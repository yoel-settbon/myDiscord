#include <gtk/gtk.h>
#include <libpq-fe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <process.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "libpq.lib")

#define MAX_MESSAGE_LENGTH 1024

GtkTextBuffer *chat_buffer;
SOCKET sock_client;
PGconn *db_conn;
int current_user_id = -1;
WSADATA wsaData;

// ------------------- Connexion à la base de données -------------------
PGconn* db_connect(const char *dbname) {
    const char *user = "postgres";
    const char *host = "localhost";
    const char *port = "5432";
    const char *password = "";
    
    char conninfo[512];
    snprintf(conninfo, sizeof(conninfo),
             "user=%s password=%s dbname=%s host=%s port=%s",
             user, password, dbname, host, port);

    printf("[DEBUG] Conninfo: %s\n", conninfo);

    PGconn *conn = PQconnectdb(conninfo);

    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Erreur de connexion à %s: %s\n", dbname, PQerrorMessage(conn));
        PQfinish(conn);
        return NULL;
    }

    return conn;
}

// ------------------- Création de la base de données -------------------
void create_database() {
    PGconn *admin_conn = db_connect("postgres");
    if (!admin_conn) {
        fprintf(stderr, "Échec de la connexion admin à postgres\n");
        return;
    }

    PGresult *res = PQexec(admin_conn,
        "SELECT 1 FROM pg_database WHERE datname = 'discord_db'");
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        fprintf(stderr, "Erreur lors de la vérification de l'existence de la base : %s\n", PQerrorMessage(admin_conn));
        PQclear(res);
        PQfinish(admin_conn);
        return;
    }

    if (PQntuples(res) == 0) {
        PQclear(res);
        res = PQexec(admin_conn, "CREATE DATABASE discord_db");
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            fprintf(stderr, "Erreur création base: %s\n", PQerrorMessage(admin_conn));
            PQclear(res);
            PQfinish(admin_conn);
            return;
        }
        printf("Base de données discord_db créée avec succès.\n");
    } else {
        printf("La base de données discord_db existe déjà.\n");
        PQclear(res);
    }

    PQfinish(admin_conn);

    PGconn *conn = db_connect("discord_db");
    if (!conn) {
        fprintf(stderr, "Échec de la connexion à discord_db\n");
        return;
    }

    const char *create_tables[] = {
        "CREATE TABLE IF NOT EXISTS \"user\" ("
        "id SERIAL PRIMARY KEY,"
        "username VARCHAR(50) UNIQUE NOT NULL,"
        "email VARCHAR(100) UNIQUE NOT NULL,"
        "password_hash VARCHAR(255) NOT NULL)",

        "CREATE TABLE IF NOT EXISTS server ("
        "id SERIAL PRIMARY KEY,"
        "name VARCHAR(100) NOT NULL,"
        "owner_id INTEGER NOT NULL REFERENCES \"user\"(id))",

        "CREATE TABLE IF NOT EXISTS canal ("
        "id SERIAL PRIMARY KEY,"
        "name VARCHAR(100) NOT NULL,"
        "attribut VARCHAR(100))",

        "CREATE TABLE IF NOT EXISTS message ("
        "id SERIAL PRIMARY KEY,"
        "content TEXT NOT NULL,"
        "user_id INTEGER NOT NULL REFERENCES \"user\"(id),"
        "canal_id INTEGER NOT NULL REFERENCES canal(id),"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP)",

        NULL
    };

    for (int i = 0; create_tables[i] != NULL; i++) {
        res = PQexec(conn, create_tables[i]);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            fprintf(stderr, "Erreur création table: %s\n", PQerrorMessage(conn));
            PQclear(res);
            PQfinish(conn);
            return;
        }
        PQclear(res);
    }

    printf("Base de données discord_db initialisée avec succès!\n");
    PQfinish(conn);
}

// ------------------- Fonctions pour le chat réseau -------------------
void append_message(const char *msg) {
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(chat_buffer, &end);
    gtk_text_buffer_insert(chat_buffer, &end, msg, -1);
    gtk_text_buffer_insert(chat_buffer, &end, "\n", -1);
}

unsigned __stdcall receive_messages(void *arg) {
    char buffer[MAX_MESSAGE_LENGTH];
    while (1) {
        int len = recv(sock_client, buffer, sizeof(buffer) - 1, 0);
        if (len <= 0) break;
        buffer[len] = '\0';
        g_idle_add((GSourceFunc)append_message, g_strdup(buffer));
    }
    return 0;
}

// ------------------- Interface GTK -------------------
static void on_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data) {
    gtk_window_destroy(GTK_WINDOW(dialog));
}

static void show_alert(GtkWindow *parent, const char *title, const char *message) {
    GtkWidget *dialog = gtk_message_dialog_new(parent,
                                              GTK_DIALOG_MODAL,
                                              GTK_MESSAGE_INFO,
                                              GTK_BUTTONS_OK,
                                              "%s", message);
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    g_signal_connect(dialog, "response", G_CALLBACK(on_dialog_response), NULL);
    gtk_widget_show(dialog);
}

static void on_send_message(GtkWidget *widget, gpointer user_data) {
    GtkWidget *entry = GTK_WIDGET(user_data);
    const char *text = gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(entry)));
    if (strlen(text) > 0) {

        send(sock_client, text, strlen(text), 0);
        
        if (current_user_id != -1) {
            char user_id_str[16];
            snprintf(user_id_str, sizeof(user_id_str), "%d", current_user_id);
            
            PGresult *res = PQexecParams(db_conn,
                "INSERT INTO message (content, user_id, canal_id) VALUES ($1, $2, 1)",
                2, NULL, (const char*[]){text, user_id_str}, NULL, NULL, 0);
            PQclear(res);
        }
        
        gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(entry)), "", 0);
    }
}

// ------------------- Interface de discussion -------------------
static void show_chat_window(GtkApplication *app, PGconn *conn, int user_id) {
    current_user_id = user_id;
    
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "Échec de l'initialisation de Winsock\n");
        return;
    }

    sock_client = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_client == INVALID_SOCKET) {
        fprintf(stderr, "Échec de la création du socket\n");
        WSACleanup();
        return;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock_client, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Échec de la connexion au serveur\n");
        closesocket(sock_client);
        WSACleanup();
        return;
    }

    _beginthreadex(NULL, 0, receive_messages, NULL, 0, NULL);

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "MyDiscord - Chat");
    gtk_window_set_default_size(GTK_WINDOW(window), 1200, 800);

    GtkWidget *main_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_window_set_child(GTK_WINDOW(window), main_paned);

    GtkWidget *channels_frame = gtk_frame_new("Canaux");
    gtk_paned_set_start_child(GTK_PANED(main_paned), channels_frame);
    
    GtkWidget *channels_list = gtk_list_box_new();
    gtk_frame_set_child(GTK_FRAME(channels_frame), channels_list);
    
    GtkWidget *chat_frame = gtk_frame_new("Discussion");
    gtk_paned_set_end_child(GTK_PANED(main_paned), chat_frame);
    
    GtkWidget *chat_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_frame_set_child(GTK_FRAME(chat_frame), chat_box);
    
    GtkWidget *messages_scroll = gtk_scrolled_window_new();
    gtk_box_append(GTK_BOX(chat_box), messages_scroll);
    
    GtkWidget *messages_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(messages_view), FALSE);
    chat_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(messages_view));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(messages_scroll), messages_view);
    
    GtkWidget *message_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(message_entry), "Écrivez votre message...");
    gtk_box_append(GTK_BOX(chat_box), message_entry);
    
    GtkWidget *send_button = gtk_button_new_with_label("Envoyer");
    g_signal_connect(send_button, "clicked", G_CALLBACK(on_send_message), message_entry);
    gtk_box_append(GTK_BOX(chat_box), send_button);

    PGresult *res = PQexec(conn, "SELECT id, name FROM canal ORDER BY name");
    if (PQresultStatus(res) == PGRES_TUPLES_OK) {
        for (int i = 0; i < PQntuples(res); i++) {
            GtkWidget *label = gtk_label_new(PQgetvalue(res, i, 1));
            gtk_list_box_append(GTK_LIST_BOX(channels_list), label);
        }
    }
    PQclear(res);

    res = PQexec(conn, 
        "SELECT u.username, m.content FROM message m "
        "JOIN \"user\" u ON m.user_id = u.id "
        "WHERE m.canal_id = 1 ORDER BY m.created_at");
    if (PQresultStatus(res) == PGRES_TUPLES_OK) {
        for (int i = 0; i < PQntuples(res); i++) {
            char message[1024];
            snprintf(message, sizeof(message), "%s: %s", 
                    PQgetvalue(res, i, 0), PQgetvalue(res, i, 1));
            append_message(message);
        }
    }
    PQclear(res);

    gtk_window_present(GTK_WINDOW(window));
}

// ------------------- Gestion du bouton d'inscription -------------------
static void on_signup_clicked(GtkWidget *widget, gpointer user_data) {
    PGconn *conn = (PGconn*)user_data;
    GtkWindow *window = GTK_WINDOW(gtk_widget_get_root(widget));

    GtkWidget *username_entry = (GtkWidget*)g_object_get_data(G_OBJECT(window), "username_entry");
    GtkWidget *email_entry = (GtkWidget*)g_object_get_data(G_OBJECT(window), "signup_email_entry");
    GtkWidget *password_entry = (GtkWidget*)g_object_get_data(G_OBJECT(window), "signup_password_entry");

    const char *username = gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(username_entry)));
    const char *email = gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(email_entry)));
    const char *password = gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(password_entry)));

    if (strlen(username) < 3 || strlen(email) < 5 || strlen(password) < 6) {
        show_alert(window, "Erreur", "Tous les champs doivent être remplis (mot de passe 6 caractères minimum)");
        return;
    }

    PGresult *res = PQexecParams(conn,
        "INSERT INTO \"user\" (username, email, password_hash) VALUES ($1, $2, $3) RETURNING id",
        3, NULL, (const char*[]){username, email, password}, NULL, NULL, 0);

    if (PQresultStatus(res) == PGRES_TUPLES_OK) {
        show_alert(window, "Succès", "Compte créé avec succès !");
        gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(username_entry)), "", 0);
        gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(email_entry)), "", 0);
        gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(password_entry)), "", 0);
    } else {
        const char *error = PQerrorMessage(conn);
        show_alert(window, "Erreur", strstr(error, "user_email_key") ? 
                  "Email déjà utilisé" : "Erreur de création de compte");
    }
    PQclear(res);
}

// ------------------- Gestion du bouton de connexion -------------------
static void on_login_clicked(GtkWidget *widget, gpointer user_data) {
    PGconn *conn = (PGconn*)user_data;
    GtkWindow *window = GTK_WINDOW(gtk_widget_get_root(widget));
    GtkApplication *app = GTK_APPLICATION(g_object_get_data(G_OBJECT(window), "app"));

    GtkWidget *email_entry = (GtkWidget*)g_object_get_data(G_OBJECT(window), "email_entry");
    GtkWidget *password_entry = (GtkWidget*)g_object_get_data(G_OBJECT(window), "password_entry");

    const char *email = gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(email_entry)));
    const char *password = gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(password_entry)));

    PGresult *res = PQexecParams(conn,
        "SELECT id FROM \"user\" WHERE email = $1 AND password_hash = $2",
        2, NULL, (const char*[]){email, password}, NULL, NULL, 0);

    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        int user_id = atoi(PQgetvalue(res, 0, 0));
        gtk_window_destroy(window);
        show_chat_window(app, conn, user_id);
    } else {
        show_alert(window, "Échec", "Email ou mot de passe incorrect");
    }
    PQclear(res);
}

// ------------------- Interface de connexion -------------------
static void activate(GtkApplication *app, gpointer user_data) {
    PGconn *conn = (PGconn*)user_data;

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "MyDiscord - Connexion");
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 300);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(main_box, 15);
    gtk_widget_set_margin_end(main_box, 15);
    gtk_widget_set_margin_top(main_box, 15);
    gtk_widget_set_margin_bottom(main_box, 15);
    gtk_window_set_child(GTK_WINDOW(window), main_box);

    GtkWidget *notebook = gtk_notebook_new();
    gtk_box_append(GTK_BOX(main_box), notebook);

    GtkWidget *login_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), login_box, gtk_label_new("Connexion"));

    GtkWidget *email_label = gtk_label_new("Email:");
    gtk_widget_set_halign(email_label, GTK_ALIGN_START);
    GtkWidget *email_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(email_entry), "email@exemple.com");

    GtkWidget *password_label = gtk_label_new("Mot de passe:");
    gtk_widget_set_halign(password_label, GTK_ALIGN_START);
    GtkWidget *password_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(password_entry), FALSE);

    GtkWidget *login_button = gtk_button_new_with_label("Se connecter");
    g_signal_connect(login_button, "clicked", G_CALLBACK(on_login_clicked), conn);

    gtk_box_append(GTK_BOX(login_box), email_label);
    gtk_box_append(GTK_BOX(login_box), email_entry);
    gtk_box_append(GTK_BOX(login_box), password_label);
    gtk_box_append(GTK_BOX(login_box), password_entry);
    gtk_box_append(GTK_BOX(login_box), login_button);

    GtkWidget *signup_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), signup_box, gtk_label_new("Inscription"));

    GtkWidget *username_label = gtk_label_new("Nom d'utilisateur:");
    GtkWidget *username_entry = gtk_entry_new();
    GtkWidget *signup_email_label = gtk_label_new("Email:");
    GtkWidget *signup_email_entry = gtk_entry_new();
    GtkWidget *signup_password_label = gtk_label_new("Mot de passe:");
    GtkWidget *signup_password_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(signup_password_entry), FALSE);

    GtkWidget *signup_button = gtk_button_new_with_label("Créer un compte");
    g_signal_connect(signup_button, "clicked", G_CALLBACK(on_signup_clicked), conn);

    gtk_box_append(GTK_BOX(signup_box), username_label);
    gtk_box_append(GTK_BOX(signup_box), username_entry);
    gtk_box_append(GTK_BOX(signup_box), signup_email_label);
    gtk_box_append(GTK_BOX(signup_box), signup_email_entry);
    gtk_box_append(GTK_BOX(signup_box), signup_password_label);
    gtk_box_append(GTK_BOX(signup_box), signup_password_entry);
    gtk_box_append(GTK_BOX(signup_box), signup_button);

    g_object_set_data(G_OBJECT(window), "email_entry", email_entry);
    g_object_set_data(G_OBJECT(window), "password_entry", password_entry);
    g_object_set_data(G_OBJECT(window), "username_entry", username_entry);
    g_object_set_data(G_OBJECT(window), "signup_email_entry", signup_email_entry);
    g_object_set_data(G_OBJECT(window), "signup_password_entry", signup_password_entry);
    g_object_set_data(G_OBJECT(window), "app", app);

    gtk_window_present(GTK_WINDOW(window));
}

// ------------------- Fonction principale -------------------
int main(int argc, char *argv[]) {
    create_database();

    db_conn = db_connect("discord_db");
    if (!db_conn) {
        fprintf(stderr, "Impossible de se connecter à discord_db\n");
        return 1;
    }

    GtkApplication *app = gtk_application_new("org.gtk.discord", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), db_conn);

    int status = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);
    PQfinish(db_conn);
    if (sock_client != INVALID_SOCKET) {
        closesocket(sock_client);
        WSACleanup();
    }
    
    return status;
}
