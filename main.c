#include <stdlib.h>
#include <gtk/gtk.h>

#include <libxml/xmlreader.h>
#include <malloc.h>
#include <string.h>
#include <stdbool.h>

#define FORM_VGROUP 1
#define FORM_HGROUP 2

#define GTK_CHAR(string) (const gchar *)(string)
#define XSTRING(string) (const xmlChar *) (string)
#define CHAR(string) (const char *)(string)

typedef struct __form_element {

    int             type;
    int             id;
    gchar          *name;
    gchar          *title;
    int             table_index;
    bool            box;
    GtkWidget      *widget;

    void           *data;

    GArray         *elements;

} form_element_t;

static inline int
xstrcmp(const xmlChar *xs, const char *b)
{
    return strcmp((const char *)xs, b);
}


static inline const xmlChar *
children_content(const xmlNode *p)
{
    if (!p)
        return 0;
    if (!p->children)
        return  0;
    return p->children->content;
}


static inline bool
is_oftype(const xmlNode *node, const char *type)
{
    return (node->type == XML_ELEMENT_NODE)
        && (xstrcmp(node->name, type) == 0)
    ;
}

static const xmlNode *
find_by_name(const xmlNode *list, const char *name)
{
    const xmlNode *cur_node = NULL;
    for (cur_node = list; cur_node; cur_node = cur_node->next) {
        if (cur_node->type == XML_ELEMENT_NODE) {
            if (xstrcmp(cur_node->name, name) == 0)
                return cur_node;
        }
    }
    return 0;
}

static const xmlNode *
find_by_path(const xmlNode *root, const char *path)
{
    char *subs = malloc(strlen(path) + 1);
    const xmlNode *p = root;

    while (*path) {
        int l = strlen(path);
        char *k = strstr(path, "/");
        if (k == 0) {
            strcpy(subs, path);
            path = path + l;
        } else {
            l = k - path;
            strncpy(subs, path, l);
            subs[l] = 0;
            path = k + 1;
        }
        p = find_by_name(p->children, subs);
        if (!p)
            break;
    }
    free(subs);
    return p;
}

static void
container_add(form_element_t *f, GtkWidget *child)
{
    if (f->box)
        gtk_box_pack_start(GTK_BOX(f->widget), child, true, true, 0);
    else
        gtk_container_add(GTK_CONTAINER(f->widget), child);
}

static const xmlChar *
entry_title(const xmlNode *p)
{
    /* TODO: Решить вопрос с префиксами пространств имён v8: */
    p = find_by_path(p, "Properties/Title/item/content");
    return children_content(p);
}

static const xmlChar *
entry_title_ex(const xmlNode *p)
{
    const xmlNode *e = find_by_path(p, "Properties/Title/item/content");
    const xmlChar *r = 0;

    if (e)
        r = children_content(e);

    if (r)
        return r;

    e = find_by_path(p, "Properties/Name");
    if (e)
        r = children_content(e);
    if (r) {
        char *buf = malloc(strlen(CHAR(r)) + 3);
        sprintf(buf, "[%s]", r);
        r = XSTRING (g_strdup(buf));
        free(buf);
        return r;
    }
    return 0;
}



#define GROUPING_VERTICAL 0
#define GROUPING_HORIZONTAL 1

static void
load_form_element(const xmlNode *cur_node, form_element_t *parent);

static void
load_form_elements(const xmlNode *list, form_element_t *parent);

static void
load_text_entry(const xmlNode *text, form_element_t *parent, bool in_table);

#define REPRESENTATION_NONE   0
#define REPRESENTATION_WEAK   1
#define REPRESENTATION_USUAL  2
#define REPRESENTATION_STRONG 3

static void
load_usual_group(const xmlNode *group, form_element_t *parent)
{
    const xmlNode *p = find_by_path(group, "Properties/Group")->children;

    int grouping = xstrcmp(p->content, "Horizontal") == 0
            ? GROUPING_HORIZONTAL
            : GROUPING_VERTICAL
    ;
    bool show_title = true;
    p = find_by_path(group, "Properties/ShowTitle");
    if (xstrcmp(children_content(p), "false") == 0)
        show_title = false;
    const xmlChar *title = entry_title_ex(group);

    int repr = REPRESENTATION_NONE;

    p = find_by_path(group, "Properties/Representation");
    const xmlChar *s = children_content(p);
    if (xstrcmp(s, "StrongSeparation") == 0)
        repr = REPRESENTATION_STRONG;

    form_element_t G;

    G.widget = grouping == GROUPING_HORIZONTAL
        ? gtk_hbox_new(false, repr == REPRESENTATION_STRONG ? 2 : 0)
        : gtk_vbox_new(false, repr == REPRESENTATION_STRONG ? 2 : 0)
    ;
    G.box = true;

    p = find_by_path(group, "ContainedItems");
    if (p) {

        int count_table;
        const xmlNode *cur_node = p->children;

        while (cur_node) {

            count_table = 0;

            if (grouping == GROUPING_VERTICAL) {
                const xmlNode *n = cur_node;
                while (n) {
                    if (is_oftype(n, "Text") || is_oftype(n, "Input")) {
                        ++count_table;
                        n = n->next;

                        while (n && n->type != XML_ELEMENT_NODE)
                            n = n->next;

                        if (!n)
                            break;
                    } else
                        break;
                }
            }

            if ((count_table > 1) && (grouping == GROUPING_VERTICAL)) {

                form_element_t T;
                T.widget = gtk_table_new(count_table, 2, false);
                T.table_index = 0;

                container_add(&G, T.widget);

                while (count_table--) {
                    if (is_oftype(cur_node, "Text") || is_oftype(cur_node, "Input")) {
                        load_text_entry(cur_node, &T, true);
                        ++T.table_index;
                    }
                    cur_node = cur_node->next;
                    while (cur_node && cur_node->type != XML_ELEMENT_NODE)
                        cur_node = cur_node->next;
                }
            }
            else {
                load_form_element(cur_node, &G);
                cur_node = cur_node->next;
            }
        }

    }

    if (repr == REPRESENTATION_STRONG) {
        GtkWidget *widget = gtk_frame_new("");

        if (show_title)
            gtk_frame_set_label(GTK_FRAME(widget), GTK_CHAR(title));

        gtk_container_add(GTK_CONTAINER(widget), G.widget);
        container_add(parent, widget);
    } else
        container_add(parent, G.widget);
}

#define TITLE_LOCATION_NONE  0
#define TITLE_LOCATION_LEFT  1
#define TITLE_LOCATION_RIGHT 2

static void
justify_element(GtkWidget *el, const xmlNode *justify)
{
    const xmlChar *s = children_content(justify);
    gfloat xalign, yalign;
    gtk_misc_get_alignment(GTK_MISC(el), &xalign, &yalign);

    if (xstrcmp(s, "Right") == 0)
        gtk_misc_set_alignment(GTK_MISC(el), 1, yalign);
    if (xstrcmp(s, "Left") == 0)
        gtk_misc_set_alignment(GTK_MISC(el), 0, yalign);
    if (xstrcmp(s, "Auto") == 0)
        gtk_misc_set_alignment(GTK_MISC(el), 0, yalign);

}

static void
load_text_entry(const xmlNode *text, form_element_t *parent, bool in_table)
{
    const xmlChar *label = entry_title_ex(text);
    const xmlNode *p = find_by_path(text, "Properties/TitleLocation");

    int title_loc = TITLE_LOCATION_LEFT;

    {
        const xmlChar *xc_title_loc = children_content(p);
        if (xc_title_loc) {
            if (xstrcmp(xc_title_loc, "None") == 0)
                title_loc = TITLE_LOCATION_NONE;
            if (xstrcmp(xc_title_loc, "Right") == 0)
                title_loc = TITLE_LOCATION_RIGHT;
        }
    }

    bool is_edit = true;

    p = find_by_path(text, "Properties/Type");
    if (p)
        if (xstrcmp(children_content(p), "LabelField") == 0)
            is_edit = false;


    form_element_t E;
    E.box = false;

    if (label && title_loc != TITLE_LOCATION_NONE) {
        E.widget = gtk_hbox_new(false, 0);

        GtkWidget *w_label = gtk_label_new(GTK_CHAR(label));
        GtkWidget *w_edit = is_edit
            ? gtk_entry_new()
            : gtk_label_new("...")
        ;

        p = find_by_path(text, "Properties/HeaderHorizontalAlign");
        if (p)
            justify_element(w_label, p);

        if (!is_edit) {
            p = find_by_path(text, "Properties/HorizontalAlign");
            if (p)
                justify_element(w_edit, p);
        }

        if (in_table) {
            gtk_table_attach(GTK_TABLE(parent->widget),
                    w_label,
                    0, 1, parent->table_index + 1, parent->table_index + 2,
                    GTK_FILL, 0,
                    0, 0
            );
            gtk_table_attach(GTK_TABLE(parent->widget),
                    w_edit,
                    1, 2, parent->table_index + 1, parent->table_index + 2,
                    GTK_EXPAND | GTK_FILL, 0,
                    0, 0
            );
        } else {
            if (title_loc == TITLE_LOCATION_LEFT)
                gtk_box_pack_start(GTK_BOX(E.widget), w_label, false, false, 0);

            gtk_box_pack_start(GTK_BOX(E.widget), w_edit, true, true, 0);

            if (title_loc == TITLE_LOCATION_RIGHT)
                gtk_box_pack_start(GTK_BOX(E.widget), w_label, false, false, 0);

            container_add(parent, E.widget);
        }

    } else {
        if (is_edit)
            E.widget = gtk_entry_new();
        else {
            E.widget = gtk_label_new(GTK_CHAR(label));

            p = find_by_path(text, "Properties/HorizontalAlign");
            if (p)
                justify_element(E.widget, p);
        }

        if (in_table) {
            gtk_table_attach(GTK_TABLE(parent->widget),
                    E.widget,
                    1, 2, parent->table_index + 1, parent->table_index + 2,
                    GTK_EXPAND | GTK_FILL, 0,
                    0, 0
            );
        } else {
            container_add(parent, E.widget);
        }
    }
}

static void
load_label(const xmlNode *text, form_element_t *parent)
{
    const xmlChar *label = entry_title_ex(text);
    form_element_t L;

    L.widget = gtk_label_new(GTK_CHAR(label));
    L.box = false;

    justify_element(L.widget, find_by_path(text, "Properties/HeaderHorizontalAlign"));

    container_add(parent, L.widget);
}


static void
load_input(const xmlNode *text, form_element_t *parent)
{
    load_text_entry(text, parent, false);
}

static void
load_check_box(const xmlNode *box, form_element_t *parent)
{
    const xmlChar *label = entry_title_ex(box);

    form_element_t CB;
    CB.widget = gtk_check_button_new_with_label(GTK_CHAR(label));
    CB.box = false;

    container_add(parent, CB.widget);
}

static void
load_page(const xmlNode *page, form_element_t *parent)
{
    const xmlChar *label = entry_title_ex(page);
    const xmlNode *p = find_by_path(page, "Properties/Grouping");

    int grouping = GROUPING_HORIZONTAL;
    const xmlChar *c_grouping = children_content(p);
    if (c_grouping)
        grouping = xstrcmp(c_grouping, "Horizontal") == 0
                ? GROUPING_HORIZONTAL
                : GROUPING_VERTICAL
        ;

    form_element_t P;
    P.widget = grouping == GROUPING_HORIZONTAL
        ? gtk_hbox_new(false, 0)
        : gtk_vbox_new(false, 0)
    ;
    P.box = true;

    p = find_by_path(page, "ContainedItems");
    if (p)
        load_form_elements(p->children, &P);

    GtkWidget *w_label = gtk_label_new(GTK_CHAR(label));

    gtk_notebook_append_page(GTK_NOTEBOOK(parent->widget), P.widget, w_label);
}

#define PAGES_NO 0
#define PAGES_ON_TOP 1

static void
load_pages(const xmlNode *pages, form_element_t *parent)
{
    const xmlNode *items = find_by_path(pages, "ContainedItems");
    const xmlNode *p = find_by_path(pages, "Properties/PagesRepresentation");

    int pages_rep = PAGES_NO;
    if (p)
        if (p->children)
            if (p->children->content) {

                if (xstrcmp(p->children->content, "TabsOnTop") == 0)
                    pages_rep = PAGES_ON_TOP;
            }

    form_element_t P;
    P.widget = gtk_notebook_new();
    P.box = false;

    if (pages_rep == PAGES_NO)
        gtk_notebook_set_show_tabs(GTK_NOTEBOOK(P.widget), false);

    const xmlNode *cur_node = NULL;
    for (cur_node = items->children; cur_node; cur_node = cur_node->next) {
        if (is_oftype(cur_node, "Page")) {
            load_page(cur_node, &P);
        }
    }

    container_add(parent, P.widget);

}

static int
table_calc_columns_count(const xmlNode *table)
{
    const xmlNode *cur_node = NULL;
    int res = 0;
    for (cur_node = table->children; cur_node; cur_node = cur_node->next) {
        if (is_oftype(cur_node, "Text"))
            ++res;
        if (is_oftype(cur_node, "Input"))
            ++res;
        if (is_oftype(cur_node, "Columns"))
            res += table_calc_columns_count(find_by_path(cur_node, "ContainedItems"));
    }
    return res;
}

static void
table_make_columns(const xmlNode *table, GtkTreeView *view)
{
    const xmlNode *p = find_by_path(table, "ContainedItems");
    const xmlNode *cur_node = NULL;

    for (cur_node = p->children; cur_node; cur_node = cur_node->next) {
        if (is_oftype(cur_node, "Text")) {
            const xmlChar *title = entry_title_ex(cur_node);
            GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
            GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes (GTK_CHAR(title), renderer, NULL);
            gtk_tree_view_append_column(view, c);
        }
        if (is_oftype(cur_node, "Input")) {
            const xmlChar *title = entry_title_ex(cur_node);
            GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
            GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes (GTK_CHAR(title), renderer, NULL);
            gtk_tree_view_append_column(view, c);
        }
        if (is_oftype(cur_node, "Columns"))
            table_make_columns(cur_node, view);
    }
}

static GtkTreeModel *
create_tree_model(const xmlNode *table)
{
    const xmlNode *p = find_by_path(table, "ContainedItems");
    int columns_count = table_calc_columns_count(p);

    GType *_types = g_malloc_n(columns_count, sizeof(GType));
    int i = columns_count;
    while (i--)
        _types[i] = G_TYPE_STRING;

    GtkListStore *model = gtk_list_store_newv(columns_count, _types);

    g_free(_types);

    return GTK_TREE_MODEL(model);
}

static void
load_table(const xmlNode *table, form_element_t *parent)
{
    form_element_t T;

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkTreeModel *model = create_tree_model(table);
    T.widget = gtk_tree_view_new_with_model(model);

    table_make_columns(table, GTK_TREE_VIEW(T.widget));

    gtk_container_add(GTK_CONTAINER(scroll), T.widget);
    container_add(parent, scroll);
}

static void
load_form_element(const xmlNode *cur_node, form_element_t *parent)
{
    if (is_oftype(cur_node, "Group")) {
        const xmlNode *p = find_by_path(cur_node, "Properties/Type")->children;

        if (xstrcmp(p->content, "UsualGroup") == 0)
            load_usual_group(cur_node, parent);
    }

    if (is_oftype(cur_node, "Text")) {
        load_text_entry(cur_node, parent, false);
    }

    if (is_oftype(cur_node, "Input")) {
        load_input(cur_node, parent);
    }

    if (is_oftype(cur_node, "CheckBox")) {
        load_check_box(cur_node, parent);
    }

    if (is_oftype(cur_node, "Pages")) {
        load_pages(cur_node, parent);
    }

    if (is_oftype(cur_node, "Table")) {
        load_table(cur_node, parent);
    }

}

static void
load_form_elements(const xmlNode *list, form_element_t *parent)
{
    const xmlNode *cur_node = NULL;
    for (cur_node = list; cur_node; cur_node = cur_node->next)
        load_form_element(cur_node, parent);
}


static form_element_t *
read_83_form(const char *path)
{

    xmlDoc *doc = NULL;
    xmlNode *root_element = NULL;
    const xmlNode *el;

    doc = xmlReadFile(path, NULL, 0);
    root_element = xmlDocGetRootElement(doc);

    el = find_by_name(root_element->children, "Elements");

    int grouping = GROUPING_VERTICAL;

    {
        const xmlNode *p = find_by_path(el, "Properties/ChildrenGrouping")->children;

        if (xstrcmp(p->content, "Vertical") != 0)
            grouping = GROUPING_HORIZONTAL;
    }

    el = find_by_path(el, "ContainedItems");

    form_element_t *form = malloc(sizeof(*form));

    form->widget = grouping == GROUPING_HORIZONTAL
            ? gtk_hbox_new(false, 2)
            : gtk_vbox_new(false, 2)
    ;
    form->box = true;

    load_form_elements(el->children, form);

    xmlFreeDoc(doc);

    xmlCleanupParser();

    return form;
}

int main (int argc, char *argv[])
{
    GtkWidget *win = NULL;

    g_log_set_handler ("Gtk", G_LOG_LEVEL_WARNING, (GLogFunc) gtk_false, NULL);
    gtk_init (&argc, &argv);
    g_log_set_handler ("Gtk", G_LOG_LEVEL_WARNING, g_log_default_handler, NULL);

    form_element_t *form = read_83_form("./Form.xml");
    if (!form) {
        fprintf(stderr, "failed to open Form.xml!\n");
        return 1;
    }

    win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_container_set_border_width (GTK_CONTAINER (win), 8);
    gtk_window_set_title (GTK_WINDOW (win), "1C 8.3 Managed Form in GTK+");
    gtk_window_set_position (GTK_WINDOW (win), GTK_WIN_POS_CENTER);
    gtk_widget_realize (win);
    g_signal_connect (win, "destroy", gtk_main_quit, NULL);

    gtk_container_add (GTK_CONTAINER (win), form->widget);

    gtk_widget_show_all (win);
    gtk_main ();
    return 0;
}
