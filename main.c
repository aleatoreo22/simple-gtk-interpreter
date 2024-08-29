#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <linux/limits.h>
#include <gtk/gtk.h>

#define BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))

GtkWidget *main_window;
GtkWidget *main_frame;
gchar *source_file = NULL;
int inotify_fd;
int watch_fd;
GIOChannel *channel;
GtkWidget *interpreter_widget = NULL;

gboolean check_file_update(GIOChannel *source, GIOCondition condition, gpointer data);
void run_notify();
char *compile();
static void run_builder();
int notify();
void start_interpreter();
static void activate(GtkApplication *app, gpointer user_data);

gboolean check_file_update(GIOChannel *source, GIOCondition condition, gpointer data)
{
    char buf[BUF_LEN] __attribute__((aligned(8)));
    ssize_t numRead;
    struct inotify_event *event;
    numRead = read(inotify_fd, buf, BUF_LEN);
    if (numRead <= 0)
        return G_SOURCE_CONTINUE;
    for (char *ptr = buf; ptr < buf + numRead;)
    {
        event = (struct inotify_event *)ptr;
        if (event->mask & IN_MODIFY)
            run_builder();
        ptr += sizeof(struct inotify_event) + event->len;
    }
    return G_SOURCE_CONTINUE;
}

void run_notify()
{
    inotify_fd = inotify_init();
    if (inotify_fd == -1)
    {
        perror("inotify_init");
        exit(EXIT_FAILURE);
    }
    watch_fd = inotify_add_watch(inotify_fd, source_file, IN_MODIFY);
    if (watch_fd == -1)
    {
        perror("inotify_add_watch");
        exit(EXIT_FAILURE);
    }
    channel = g_io_channel_unix_new(inotify_fd);
    g_io_add_watch(channel, G_IO_IN, check_file_update, NULL);
}

char *compile()
{
    FILE *fp;
    char aux[200];
    char command[32 + sizeof(source_file)] = "/bin/blueprint-compiler compile ";
    char *output = NULL;
    int output_len = 0;
    strcat(command, source_file);
    fp = popen(command, "r");
    if (fp == NULL)
    {
        printf("Failed to run command\n");
        exit(1);
    }
    while (fgets(aux, sizeof(aux), fp) != NULL)
    {
        int aux_len = strlen(aux);
        char *temp = realloc(output, output_len + aux_len + 1);
        if (temp == NULL)
        {
            printf("Memory allocation failed\n");
            free(output);
            pclose(fp);
            exit(1);
        }
        output = temp;
        strcpy(output + output_len, aux);
        output_len += aux_len;
    }
    pclose(fp);
    return output;
}

static void run_builder()
{
    GtkBuilder *builder = gtk_builder_new();
    GError *error = NULL;
    char *compiled_ui = compile();
    if (compiled_ui == NULL)
        return;
    gsize size = strlen(compiled_ui);
    if (!gtk_builder_add_from_string(builder, compiled_ui, size, &error))
    {
        g_printerr("Error loading file: %s\n", error->message);
        return;
    }
    interpreter_widget = GTK_WIDGET(gtk_builder_get_object(builder, "main"));
    gtk_widget_unparent(interpreter_widget);
    gtk_frame_set_child(GTK_FRAME(main_frame), interpreter_widget);
    g_object_unref(builder);
}

int notify()
{
    int inotify_fd = inotify_init();
    if (inotify_fd == -1)
    {
        perror("inotify_init");
        exit(EXIT_FAILURE);
    }
    int watch_fd = inotify_add_watch(inotify_fd, "window.blp", IN_MODIFY);
    if (watch_fd == -1)
    {
        perror("inotify_add_watch");
        exit(EXIT_FAILURE);
    }
    char buf[BUF_LEN] __attribute__((aligned(8)));
    ssize_t numRead;
    struct inotify_event *event;
    while (1)
    {
        numRead = read(inotify_fd, buf, BUF_LEN);
        if (numRead == -1)
        {
            perror("read");
            exit(EXIT_FAILURE);
        }
        for (char *ptr = buf; ptr < buf + numRead;)
        {
            event = (struct inotify_event *)ptr;
            if (event->mask & IN_MODIFY)
            {
                run_builder();
            }
            ptr += sizeof(struct inotify_event) + event->len;
        }
    }
    close(inotify_fd);
    return 0;
}

void start_interpreter()
{
    source_file = "window.blp";
    run_builder();
    run_notify();
}

static void activate(GtkApplication *app, gpointer user_data)
{
    GtkWidget *button;
    main_window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(main_window), "Window");
    gtk_window_set_default_size(GTK_WINDOW(main_window), 200, 200);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(main_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(main_box, GTK_ALIGN_CENTER);
    gtk_window_set_child(GTK_WINDOW(main_window), main_box);

    main_frame = gtk_frame_new("Interpreter");
    gtk_box_append(GTK_BOX(main_box), main_frame);

    gtk_window_present(GTK_WINDOW(main_window));
    start_interpreter();
}

int main(int argc, char **argv)
{
    GtkApplication *app;
    int status;
    app = gtk_application_new("org.gtk.example", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    close(inotify_fd);
    return status;
}
