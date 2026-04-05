/*
 * Library Management System (CLI) — single-file C implementation
 *
 * Data management (mandatory structures):
 *   - Binary search trees (BST): book catalog by book_id, student registry by student_id
 *   - Queues (linked-list FIFO): active loans, waitlist when copies are unavailable
 *
 * Persistence: books.dat, students.dat, issues.dat, history.dat, admin.dat, waitlist.dat
 *
 * Compile: gcc library.c -o library
 * Run:     ./library   (or library.exe on Windows)
 *
 * Default admin (created if admin.dat missing):  admin / admin123
 * Sample students (seeded if data files empty):
 *   ID 1001  username alice   password alice123
 *   ID 1002  username bob     password bob123
 *   ID 1003  username carol   password carol123
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* -------------------------------------------------------------------------- */
/* Configuration                                                              */
/* -------------------------------------------------------------------------- */

#define BOOK_FILE      "books.dat"
#define STUDENT_FILE   "students.dat"
#define ISSUE_FILE     "issues.dat"
#define HISTORY_FILE   "history.dat"
#define ADMIN_FILE     "admin.dat"
#define WAITLIST_FILE  "waitlist.dat"

#define MAX_BOOKS_PER_STUDENT 3
#define LOAN_DAYS             14
#define FINE_PER_DAY          5   /* currency units per calendar day late */

#define TITLE_LEN   100
#define AUTHOR_LEN  100
#define NAME_LEN    100
#define USER_LEN    50
#define PASS_LEN    50

/* -------------------------------------------------------------------------- */
/* ANSI colors (optional UI)                                                  */
/* -------------------------------------------------------------------------- */

#ifdef _WIN32
static void enable_ansi(void) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
#else
#define enable_ansi() ((void)0)
#endif

#define CLR_RESET   "\033[0m"
#define CLR_RED     "\033[31m"
#define CLR_GREEN   "\033[32m"
#define CLR_YELLOW  "\033[33m"
#define CLR_CYAN    "\033[36m"
#define CLR_BOLD    "\033[1m"

static void clear_screen(void) {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

/* -------------------------------------------------------------------------- */
/* Core structs                                                               */
/* -------------------------------------------------------------------------- */

typedef struct {
    int y, m, d;
} Date;

typedef struct {
    int book_id;
    char title[TITLE_LEN];
    char author[AUTHOR_LEN];
    int quantity;
    int issued_count;
} Book;

typedef struct {
    int student_id;
    char username[USER_LEN];
    char name[NAME_LEN];
    char password[PASS_LEN];
} Student;

typedef struct {
    int book_id;
    int student_id;
    Date issue_date;
    Date due_date;
} ActiveIssue;

typedef struct {
    int book_id;
    int student_id;
    Date issue_date;
    Date return_date;
    int fine_amount;
    int was_late;
} IssueHistory;

typedef struct {
    char username[USER_LEN];
    char password[PASS_LEN];
} AdminCred;

/* -------------------------------------------------------------------------- */
/* BST nodes — books (key: book_id)                                           */
/* -------------------------------------------------------------------------- */

typedef struct BookNode {
    Book data;
    struct BookNode *left;
    struct BookNode *right;
} BookNode;

/* -------------------------------------------------------------------------- */
/* BST nodes — students (key: student_id)                                     */
/* -------------------------------------------------------------------------- */

typedef struct StudentNode {
    Student data;
    struct StudentNode *left;
    struct StudentNode *right;
} StudentNode;

/* -------------------------------------------------------------------------- */
/* Title-index BST for sorted-by-title views (temporary, rebuilt as needed)   */
/* -------------------------------------------------------------------------- */

typedef struct TitleNode {
    int book_id;
    char title[TITLE_LEN];
    char author[AUTHOR_LEN];
    int quantity;
    int issued_count;
    struct TitleNode *left;
    struct TitleNode *right;
} TitleNode;

/* -------------------------------------------------------------------------- */
/* Queue nodes — active issues (FIFO list; linear scan for return / rules)    */
/* -------------------------------------------------------------------------- */

typedef struct IssueQNode {
    ActiveIssue rec;
    struct IssueQNode *next;
} IssueQNode;

/* -------------------------------------------------------------------------- */
/* Queue nodes — waitlist when no stock (FIFO per global queue, book_id tag) */
/* -------------------------------------------------------------------------- */

typedef struct WaitQNode {
    int student_id;
    int book_id;
    struct WaitQNode *next;
} WaitQNode;

/* -------------------------------------------------------------------------- */
/* Library context (no globals — passed by pointer)                            */
/* -------------------------------------------------------------------------- */

typedef struct {
    BookNode *book_root;
    StudentNode *student_root;
    IssueQNode *issue_front;
    IssueQNode *issue_rear;
    WaitQNode *wait_front;
    WaitQNode *wait_rear;
} LibraryCtx;

/* -------------------------------------------------------------------------- */
/* Date helpers (time.h / calendar)                                           */
/* -------------------------------------------------------------------------- */

static Date date_from_tm(const struct tm *t) {
    Date d;
    d.y = t->tm_year + 1900;
    d.m = t->tm_mon + 1;
    d.d = t->tm_mday;
    return d;
}

static time_t date_to_time_t(Date d) {
    struct tm tmv = {0};
    tmv.tm_year = d.y - 1900;
    tmv.tm_mon = d.m - 1;
    tmv.tm_mday = d.d;
    return mktime(&tmv);
}

static Date today_date(void) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    return date_from_tm(t);
}

static Date add_days(Date start, int days) {
    time_t base = date_to_time_t(start);
    if (base == (time_t)-1)
        return start;
    base += (time_t)days * 86400;
    struct tm *t = localtime(&base);
    return date_from_tm(t);
}

static int days_between(Date from, Date to) {
    time_t a = date_to_time_t(from);
    time_t b = date_to_time_t(to);
    if (a == (time_t)-1 || b == (time_t)-1)
        return 0;
    double diff = difftime(b, a);
    return (int)(diff / 86400.0);
}

static void print_date(Date d) {
    printf("%04d-%02d-%02d", d.y, d.m, d.d);
}

/* -------------------------------------------------------------------------- */
/* Book BST                                                                   */
/* -------------------------------------------------------------------------- */

static BookNode *book_bst_search(BookNode *root, int book_id) {
    if (!root)
        return NULL;
    if (book_id == root->data.book_id)
        return root;
    if (book_id < root->data.book_id)
        return book_bst_search(root->left, book_id);
    return book_bst_search(root->right, book_id);
}

static BookNode *book_bst_insert(BookNode *root, Book b, int *err) {
    if (!root) {
        BookNode *n = (BookNode *)calloc(1, sizeof(BookNode));
        if (!n) {
            *err = 1;
            return NULL;
        }
        n->data = b;
        return n;
    }
    if (b.book_id == root->data.book_id) {
        *err = 2; /* duplicate */
        return root;
    }
    if (b.book_id < root->data.book_id)
        root->left = book_bst_insert(root->left, b, err);
    else
        root->right = book_bst_insert(root->right, b, err);
    return root;
}

static BookNode *book_min_node(BookNode *n) {
    while (n && n->left)
        n = n->left;
    return n;
}

static BookNode *book_bst_delete(BookNode *root, int book_id, int *found) {
    if (!root)
        return NULL;
    if (book_id < root->data.book_id)
        root->left = book_bst_delete(root->left, book_id, found);
    else if (book_id > root->data.book_id)
        root->right = book_bst_delete(root->right, book_id, found);
    else {
        *found = 1;
        if (!root->left) {
            BookNode *t = root->right;
            free(root);
            return t;
        }
        if (!root->right) {
            BookNode *t = root->left;
            free(root);
            return t;
        }
        BookNode *succ = book_min_node(root->right);
        root->data = succ->data;
        int f = 0;
        root->right = book_bst_delete(root->right, succ->data.book_id, &f);
    }
    return root;
}

static void book_bst_free(BookNode *root) {
    if (!root)
        return;
    book_bst_free(root->left);
    book_bst_free(root->right);
    free(root);
}

static void book_write_inorder(FILE *fp, BookNode *root) {
    if (!root)
        return;
    book_write_inorder(fp, root->left);
    fwrite(&root->data, sizeof(Book), 1, fp);
    book_write_inorder(fp, root->right);
}

/* -------------------------------------------------------------------------- */
/* Student BST                                                                */
/* -------------------------------------------------------------------------- */

static StudentNode *student_bst_search_id(StudentNode *root, int sid) {
    if (!root)
        return NULL;
    if (sid == root->data.student_id)
        return root;
    if (sid < root->data.student_id)
        return student_bst_search_id(root->left, sid);
    return student_bst_search_id(root->right, sid);
}

/* Case-insensitive ASCII compare (stdlib/string.h only; no ctype.h) */
static int ascii_tolower(int c) {
    if (c >= 'A' && c <= 'Z')
        return c + ('a' - 'A');
    return c;
}

static int str_icmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = ascii_tolower((unsigned char)*a);
        int cb = ascii_tolower((unsigned char)*b);
        if (ca != cb)
            return ca - cb;
        a++;
        b++;
    }
    return ascii_tolower((unsigned char)*a) - ascii_tolower((unsigned char)*b);
}

static StudentNode *student_bst_search_username(StudentNode *root, const char *uname) {
    if (!root)
        return NULL;
    StudentNode *x = student_bst_search_username(root->left, uname);
    if (x)
        return x;
    if (str_icmp(root->data.username, uname) == 0)
        return root;
    return student_bst_search_username(root->right, uname);
}

static StudentNode *student_bst_insert(StudentNode *root, Student s, int *err) {
    if (!root) {
        StudentNode *n = (StudentNode *)calloc(1, sizeof(StudentNode));
        if (!n) {
            *err = 1;
            return NULL;
        }
        n->data = s;
        return n;
    }
    if (s.student_id == root->data.student_id) {
        *err = 2;
        return root;
    }
    if (s.student_id < root->data.student_id)
        root->left = student_bst_insert(root->left, s, err);
    else
        root->right = student_bst_insert(root->right, s, err);
    return root;
}

static StudentNode *student_min_node(StudentNode *n) {
    while (n && n->left)
        n = n->left;
    return n;
}

static StudentNode *student_bst_delete(StudentNode *root, int sid, int *found) {
    if (!root)
        return NULL;
    if (sid < root->data.student_id)
        root->left = student_bst_delete(root->left, sid, found);
    else if (sid > root->data.student_id)
        root->right = student_bst_delete(root->right, sid, found);
    else {
        *found = 1;
        if (!root->left) {
            StudentNode *t = root->right;
            free(root);
            return t;
        }
        if (!root->right) {
            StudentNode *t = root->left;
            free(root);
            return t;
        }
        StudentNode *succ = student_min_node(root->right);
        root->data = succ->data;
        int f = 0;
        root->right = student_bst_delete(root->right, succ->data.student_id, &f);
    }
    return root;
}

static void student_bst_free(StudentNode *root) {
    if (!root)
        return;
    student_bst_free(root->left);
    student_bst_free(root->right);
    free(root);
}

static void student_write_inorder(FILE *fp, StudentNode *root) {
    if (!root)
        return;
    student_write_inorder(fp, root->left);
    fwrite(&root->data, sizeof(Student), 1, fp);
    student_write_inorder(fp, root->right);
}

/* -------------------------------------------------------------------------- */
/* Title BST (for sorting by title)                                           */
/* -------------------------------------------------------------------------- */

static TitleNode *title_bst_insert(TitleNode *root, Book *b) {
    if (!root) {
        TitleNode *n = (TitleNode *)calloc(1, sizeof(TitleNode));
        if (!n)
            return NULL;
        n->book_id = b->book_id;
        strncpy(n->title, b->title, TITLE_LEN - 1);
        strncpy(n->author, b->author, AUTHOR_LEN - 1);
        n->quantity = b->quantity;
        n->issued_count = b->issued_count;
        return n;
    }
    int c = str_icmp(b->title, root->title);
    if (c < 0)
        root->left = title_bst_insert(root->left, b);
    else if (c > 0)
        root->right = title_bst_insert(root->right, b);
    else {
        if (b->book_id < root->book_id)
            root->left = title_bst_insert(root->left, b);
        else
            root->right = title_bst_insert(root->right, b);
    }
    return root;
}

static void title_bst_build_from_booktree(TitleNode **root, BookNode *bn) {
    if (!bn)
        return;
    title_bst_build_from_booktree(root, bn->left);
    *root = title_bst_insert(*root, &bn->data);
    title_bst_build_from_booktree(root, bn->right);
}

static void title_bst_print_inorder(TitleNode *root) {
    if (!root)
        return;
    title_bst_print_inorder(root->left);
    printf("  ID %-5d | %-30s | %-22s | qty %2d | out %2d\n",
           root->book_id, root->title, root->author, root->quantity, root->issued_count);
    title_bst_print_inorder(root->right);
}

static void title_bst_free(TitleNode *root) {
    if (!root)
        return;
    title_bst_free(root->left);
    title_bst_free(root->right);
    free(root);
}

/* -------------------------------------------------------------------------- */
/* Issue queue (active loans)                                                 */
/* -------------------------------------------------------------------------- */

static void issue_queue_repair_rear(LibraryCtx *ctx);

static void issue_queue_enqueue(LibraryCtx *ctx, const ActiveIssue *r) {
    IssueQNode *n = (IssueQNode *)malloc(sizeof(IssueQNode));
    if (!n)
        return;
    n->rec = *r;
    n->next = NULL;
    if (!ctx->issue_rear) {
        ctx->issue_front = ctx->issue_rear = n;
    } else {
        ctx->issue_rear->next = n;
        ctx->issue_rear = n;
    }
}

static int issue_queue_remove(LibraryCtx *ctx, int student_id, int book_id, ActiveIssue *out) {
    IssueQNode *prev = NULL, *cur = ctx->issue_front;
    while (cur) {
        if (cur->rec.student_id == student_id && cur->rec.book_id == book_id) {
            if (out)
                *out = cur->rec;
            if (prev)
                prev->next = cur->next;
            else
                ctx->issue_front = cur->next;
            free(cur);
            issue_queue_repair_rear(ctx);
            return 1;
        }
        prev = cur;
        cur = cur->next;
    }
    return 0;
}

/* Fix rear pointer if we removed the last node incorrectly */
static void issue_queue_repair_rear(LibraryCtx *ctx) {
    if (!ctx->issue_front) {
        ctx->issue_rear = NULL;
        return;
    }
    IssueQNode *p = ctx->issue_front;
    while (p->next)
        p = p->next;
    ctx->issue_rear = p;
}

static int issue_count_student(const LibraryCtx *ctx, int student_id) {
    int c = 0;
    for (IssueQNode *p = ctx->issue_front; p; p = p->next) {
        if (p->rec.student_id == student_id)
            c++;
    }
    return c;
}

static int issue_has_same_book(const LibraryCtx *ctx, int student_id, int book_id) {
    for (IssueQNode *p = ctx->issue_front; p; p = p->next) {
        if (p->rec.student_id == student_id && p->rec.book_id == book_id)
            return 1;
    }
    return 0;
}

static void issue_queue_clear(LibraryCtx *ctx) {
    IssueQNode *p = ctx->issue_front;
    while (p) {
        IssueQNode *n = p->next;
        free(p);
        p = n;
    }
    ctx->issue_front = ctx->issue_rear = NULL;
}

static void issue_queue_save(FILE *fp, LibraryCtx *ctx) {
    for (IssueQNode *p = ctx->issue_front; p; p = p->next)
        fwrite(&p->rec, sizeof(ActiveIssue), 1, fp);
}

/* -------------------------------------------------------------------------- */
/* Waitlist queue                                                             */
/* -------------------------------------------------------------------------- */

static void wait_repair_rear(LibraryCtx *ctx) {
    if (!ctx->wait_front) {
        ctx->wait_rear = NULL;
        return;
    }
    WaitQNode *p = ctx->wait_front;
    while (p->next)
        p = p->next;
    ctx->wait_rear = p;
}

static void wait_enqueue(LibraryCtx *ctx, int student_id, int book_id) {
    WaitQNode *n = (WaitQNode *)malloc(sizeof(WaitQNode));
    if (!n)
        return;
    n->student_id = student_id;
    n->book_id = book_id;
    n->next = NULL;
    if (!ctx->wait_rear) {
        ctx->wait_front = ctx->wait_rear = n;
    } else {
        ctx->wait_rear->next = n;
        ctx->wait_rear = n;
    }
}

static void wait_queue_clear(LibraryCtx *ctx) {
    WaitQNode *p = ctx->wait_front;
    while (p) {
        WaitQNode *n = p->next;
        free(p);
        p = n;
    }
    ctx->wait_front = ctx->wait_rear = NULL;
}

/* -------------------------------------------------------------------------- */
/* File I/O                                                                   */
/* -------------------------------------------------------------------------- */

static int save_books(LibraryCtx *ctx) {
    FILE *fp = fopen(BOOK_FILE, "wb");
    if (!fp)
        return 0;
    book_write_inorder(fp, ctx->book_root);
    fclose(fp);
    return 1;
}

static int load_books(LibraryCtx *ctx) {
    FILE *fp = fopen(BOOK_FILE, "rb");
    if (!fp)
        return 1; /* empty ok */
    Book b;
    while (fread(&b, sizeof(Book), 1, fp) == 1) {
        int err = 0;
        ctx->book_root = book_bst_insert(ctx->book_root, b, &err);
        if (err == 1) {
            fclose(fp);
            return 0;
        }
    }
    fclose(fp);
    return 1;
}

static int save_students(LibraryCtx *ctx) {
    FILE *fp = fopen(STUDENT_FILE, "wb");
    if (!fp)
        return 0;
    student_write_inorder(fp, ctx->student_root);
    fclose(fp);
    return 1;
}

static int load_students(LibraryCtx *ctx) {
    FILE *fp = fopen(STUDENT_FILE, "rb");
    if (!fp)
        return 1;
    Student s;
    while (fread(&s, sizeof(Student), 1, fp) == 1) {
        int err = 0;
        ctx->student_root = student_bst_insert(ctx->student_root, s, &err);
        if (err == 1) {
            fclose(fp);
            return 0;
        }
    }
    fclose(fp);
    return 1;
}

static int save_issues(LibraryCtx *ctx) {
    FILE *fp = fopen(ISSUE_FILE, "wb");
    if (!fp)
        return 0;
    issue_queue_save(fp, ctx);
    fclose(fp);
    return 1;
}

static int load_issues(LibraryCtx *ctx) {
    FILE *fp = fopen(ISSUE_FILE, "rb");
    if (!fp)
        return 1;
    ActiveIssue r;
    while (fread(&r, sizeof(ActiveIssue), 1, fp) == 1)
        issue_queue_enqueue(ctx, &r);
    fclose(fp);
    issue_queue_repair_rear(ctx);
    return 1;
}

static int save_waitlist(LibraryCtx *ctx) {
    FILE *fp = fopen(WAITLIST_FILE, "wb");
    if (!fp)
        return 0;
    for (WaitQNode *w = ctx->wait_front; w; w = w->next) {
        fwrite(&w->student_id, sizeof(int), 1, fp);
        fwrite(&w->book_id, sizeof(int), 1, fp);
    }
    fclose(fp);
    return 1;
}

static int load_waitlist(LibraryCtx *ctx) {
    FILE *fp = fopen(WAITLIST_FILE, "rb");
    if (!fp)
        return 1;
    int sid, bid;
    while (fread(&sid, sizeof(int), 1, fp) == 1 && fread(&bid, sizeof(int), 1, fp) == 1)
        wait_enqueue(ctx, sid, bid);
    fclose(fp);
    wait_repair_rear(ctx);
    return 1;
}

static void append_history(const IssueHistory *h) {
    FILE *fp = fopen(HISTORY_FILE, "ab");
    if (!fp)
        return;
    fwrite(h, sizeof(IssueHistory), 1, fp);
    fclose(fp);
}

static int save_admin(const AdminCred *a) {
    FILE *fp = fopen(ADMIN_FILE, "wb");
    if (!fp)
        return 0;
    fwrite(a, sizeof(AdminCred), 1, fp);
    fclose(fp);
    return 1;
}

static int load_admin(AdminCred *a) {
    FILE *fp = fopen(ADMIN_FILE, "rb");
    if (!fp)
        return 0;
    if (fread(a, sizeof(AdminCred), 1, fp) != 1) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return 1;
}

/* -------------------------------------------------------------------------- */
/* Seed sample data if missing                                                */
/* -------------------------------------------------------------------------- */

static void seed_admin(void) {
    AdminCred a;
    memset(&a, 0, sizeof(a));
    strncpy(a.username, "admin", USER_LEN - 1);
    strncpy(a.password, "admin123", PASS_LEN - 1);
    save_admin(&a);
}

static void seed_students(LibraryCtx *ctx) {
    Student s1 = {0};
    s1.student_id = 1001;
    strncpy(s1.username, "alice", sizeof(s1.username) - 1);
    strncpy(s1.name, "Alice Johnson", sizeof(s1.name) - 1);
    strncpy(s1.password, "alice123", sizeof(s1.password) - 1);
    int e = 0;
    ctx->student_root = student_bst_insert(ctx->student_root, s1, &e);

    Student s2 = {0};
    s2.student_id = 1002;
    strncpy(s2.username, "bob", sizeof(s2.username) - 1);
    strncpy(s2.name, "Bob Smith", sizeof(s2.name) - 1);
    strncpy(s2.password, "bob123", sizeof(s2.password) - 1);
    e = 0;
    ctx->student_root = student_bst_insert(ctx->student_root, s2, &e);

    Student s3 = {0};
    s3.student_id = 1003;
    strncpy(s3.username, "carol", sizeof(s3.username) - 1);
    strncpy(s3.name, "Carol Lee", sizeof(s3.name) - 1);
    strncpy(s3.password, "carol123", sizeof(s3.password) - 1);
    e = 0;
    ctx->student_root = student_bst_insert(ctx->student_root, s3, &e);
}

static void seed_books(LibraryCtx *ctx) {
    Book books[] = {
        {1, "Introduction to Algorithms", "Cormen", 4, 0},
        {2, "The C Programming Language", "Kernighan & Ritchie", 3, 0},
        {3, "Clean Code", "Robert Martin", 2, 0},
        {4, "Database System Concepts", "Silberschatz", 5, 0},
        {5, "Computer Networks", "Tanenbaum", 3, 0},
    };
    for (size_t i = 0; i < sizeof(books) / sizeof(books[0]); i++) {
        int e = 0;
        ctx->book_root = book_bst_insert(ctx->book_root, books[i], &e);
    }
}

/* -------------------------------------------------------------------------- */
/* Input helpers                                                              */
/* -------------------------------------------------------------------------- */

static void flush_line(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF)
        ;
}

static int read_int(const char *prompt, int *out) {
    printf("%s", prompt);
    if (scanf("%d", out) != 1) {
        flush_line();
        return 0;
    }
    flush_line();
    return 1;
}

static void read_line(const char *prompt, char *buf, size_t maxlen) {
    printf("%s", prompt);
    if (fgets(buf, (int)maxlen, stdin) == NULL) {
        buf[0] = '\0';
        return;
    }
    size_t n = strlen(buf);
    if (n && buf[n - 1] == '\n')
        buf[n - 1] = '\0';
}

static void pause_for_user(void) {
    char buf[64];
    read_line("Press Enter to continue... ", buf, sizeof(buf));
}

/* -------------------------------------------------------------------------- */
/* Printing trees (in-order)                                                  */
/* -------------------------------------------------------------------------- */

static void print_books_by_id(BookNode *root) {
    if (!root)
        return;
    print_books_by_id(root->left);
    printf("  ID %-5d | %-30s | %-22s | qty %2d | out %2d | avail %2d\n",
           root->data.book_id, root->data.title, root->data.author,
           root->data.quantity, root->data.issued_count,
           root->data.quantity - root->data.issued_count);
    print_books_by_id(root->right);
}

static void print_students_inorder(StudentNode *root) {
    if (!root)
        return;
    print_students_inorder(root->left);
    printf("  %-6d | %-12s | %s\n",
           root->data.student_id, root->data.username, root->data.name);
    print_students_inorder(root->right);
}

static void print_separator(void) {
    printf("%s\n", "============================================================");
}

/* -------------------------------------------------------------------------- */
/* Search books (BST by id; linear scan tree for title/author)                */
/* -------------------------------------------------------------------------- */

static void book_search_by_title(BookNode *root, const char *q) {
    if (!root)
        return;
    book_search_by_title(root->left, q);
    if (strstr(root->data.title, q) != NULL)
        printf("  ID %-5d | %-30s | %-22s | avail %d\n",
               root->data.book_id, root->data.title, root->data.author,
               root->data.quantity - root->data.issued_count);
    book_search_by_title(root->right, q);
}

static void book_search_by_author(BookNode *root, const char *q) {
    if (!root)
        return;
    book_search_by_author(root->left, q);
    if (strstr(root->data.author, q) != NULL)
        printf("  ID %-5d | %-30s | %-22s | avail %d\n",
               root->data.book_id, root->data.title, root->data.author,
               root->data.quantity - root->data.issued_count);
    book_search_by_author(root->right, q);
}

/* -------------------------------------------------------------------------- */
/* Issue / return core                                                        */
/* -------------------------------------------------------------------------- */

static int book_available(Book *b) {
    return b->quantity - b->issued_count > 0;
}

/* Serve FIFO waiters for book_id; skip waiters who cannot borrow yet */
static void try_process_waitlist(LibraryCtx *ctx, int book_id) {
    BookNode *bn = book_bst_search(ctx->book_root, book_id);
    if (!bn)
        return;
    for (;;) {
        if (!book_available(&bn->data))
            return;
        WaitQNode *prev = NULL, *cur = ctx->wait_front;
        WaitQNode *found_prev = NULL, *found = NULL;
        while (cur) {
            if (cur->book_id == book_id) {
                int sid = cur->student_id;
                if (issue_count_student(ctx, sid) < MAX_BOOKS_PER_STUDENT &&
                    !issue_has_same_book(ctx, sid, book_id)) {
                    found = cur;
                    found_prev = prev;
                    break;
                }
            }
            prev = cur;
            cur = cur->next;
        }
        if (!found)
            return;
        int sid = found->student_id;
        if (found_prev)
            found_prev->next = found->next;
        else
            ctx->wait_front = found->next;
        free(found);
        wait_repair_rear(ctx);

        ActiveIssue r;
        r.book_id = book_id;
        r.student_id = sid;
        r.issue_date = today_date();
        r.due_date = add_days(r.issue_date, LOAN_DAYS);
        issue_queue_enqueue(ctx, &r);
        bn->data.issued_count++;
        printf(CLR_GREEN "Waitlist: auto-issued book %d to student %d\n" CLR_RESET, book_id, sid);
        save_books(ctx);
        save_issues(ctx);
        save_waitlist(ctx);
    }
}

static int issue_book(LibraryCtx *ctx, int student_id, int book_id) {
    BookNode *bn = book_bst_search(ctx->book_root, book_id);
    if (!bn) {
        printf(CLR_RED "Book ID not found in catalog (BST).\n" CLR_RESET);
        return 0;
    }
    if (!student_bst_search_id(ctx->student_root, student_id)) {
        printf(CLR_RED "Student ID not found.\n" CLR_RESET);
        return 0;
    }
    if (issue_has_same_book(ctx, student_id, book_id)) {
        printf(CLR_RED "Student already has this book on loan (queue check).\n" CLR_RESET);
        return 0;
    }
    if (issue_count_student(ctx, student_id) >= MAX_BOOKS_PER_STUDENT) {
        printf(CLR_RED "Borrow limit reached (max %d books).\n" CLR_RESET, MAX_BOOKS_PER_STUDENT);
        return 0;
    }
    if (!book_available(&bn->data)) {
        wait_enqueue(ctx, student_id, book_id);
        printf(CLR_YELLOW "No copies available — added to waitlist (FIFO queue).\n" CLR_RESET);
        save_waitlist(ctx);
        return 0;
    }
    ActiveIssue r;
    r.book_id = book_id;
    r.student_id = student_id;
    r.issue_date = today_date();
    r.due_date = add_days(r.issue_date, LOAN_DAYS);
    issue_queue_enqueue(ctx, &r);
    bn->data.issued_count++;
    printf(CLR_GREEN "Book issued. Due: " CLR_RESET);
    print_date(r.due_date);
    printf("\n");
    save_books(ctx);
    save_issues(ctx);
    return 1;
}

static int return_book(LibraryCtx *ctx, int student_id, int book_id) {
    ActiveIssue rec;
    if (!issue_queue_remove(ctx, student_id, book_id, &rec)) {
        printf(CLR_RED "No active loan found for this student/book (queue).\n" CLR_RESET);
        return 0;
    }

    BookNode *bn = book_bst_search(ctx->book_root, book_id);
    if (bn && bn->data.issued_count > 0)
        bn->data.issued_count--;

    Date ret = today_date();
    int late_days = days_between(rec.due_date, ret);
    int fine = 0;
    int was_late = 0;
    if (late_days > 0) {
        was_late = 1;
        fine = late_days * FINE_PER_DAY;
        printf(CLR_YELLOW "Returned late by %d day(s). Fine: %d (rate %d/day).\n" CLR_RESET,
               late_days, fine, FINE_PER_DAY);
    } else {
        printf(CLR_GREEN "Returned on time.\n" CLR_RESET);
    }

    IssueHistory h;
    memset(&h, 0, sizeof(h));
    h.book_id = book_id;
    h.student_id = student_id;
    h.issue_date = rec.issue_date;
    h.return_date = ret;
    h.fine_amount = fine;
    h.was_late = was_late;
    append_history(&h);

    save_books(ctx);
    save_issues(ctx);
    try_process_waitlist(ctx, book_id);
    save_books(ctx);
    save_issues(ctx);
    return 1;
}

/* -------------------------------------------------------------------------- */
/* Admin / Student menus                                                      */
/* -------------------------------------------------------------------------- */

static int admin_login(void) {
    AdminCred filecred;
    memset(&filecred, 0, sizeof(filecred));
    if (!load_admin(&filecred)) {
        printf(CLR_RED "Admin credentials missing.\n" CLR_RESET);
        return 0;
    }
    char u[USER_LEN], p[PASS_LEN];
    read_line("Username: ", u, sizeof(u));
    read_line("Password: ", p, sizeof(p));
    if (strcmp(u, filecred.username) == 0 && strcmp(p, filecred.password) == 0)
        return 1;
    printf(CLR_RED "Invalid admin credentials.\n" CLR_RESET);
    return 0;
}

static StudentNode *student_login(LibraryCtx *ctx) {
    char mode[8];
    read_line("Login with (I)d or (U)sername? ", mode, sizeof(mode));
    if (mode[0] == 'i' || mode[0] == 'I') {
        int sid;
        if (!read_int("Student ID: ", &sid))
            return NULL;
        return student_bst_search_id(ctx->student_root, sid);
    }
    char uname[USER_LEN];
    read_line("Username: ", uname, sizeof(uname));
    return student_bst_search_username(ctx->student_root, uname);
}

static void admin_menu(LibraryCtx *ctx) {
    for (;;) {
        clear_screen();
        print_separator();
        printf(CLR_BOLD CLR_CYAN " ADMIN DASHBOARD\n" CLR_RESET);
        print_separator();
        printf(" 1) Add book\n 2) View all books (by ID)\n 3) View books sorted by title (title BST)\n");
        printf(" 4) Search books\n 5) Update book\n 6) Delete book\n");
        printf(" 7) View issued books (issue queue)\n 8) Add student\n 9) Remove student\n");
        printf("10) View all students\n11) View waitlist\n 0) Logout\n");
        print_separator();
        int ch;
        if (!read_int("Choice: ", &ch))
            continue;

        if (ch == 0)
            break;

        if (ch == 1) {
            Book b;
            memset(&b, 0, sizeof(b));
            read_int("Book ID (unique): ", &b.book_id);
            read_line("Title: ", b.title, sizeof(b.title));
            read_line("Author: ", b.author, sizeof(b.author));
            read_int("Quantity: ", &b.quantity);
            b.issued_count = 0;
            if (book_bst_search(ctx->book_root, b.book_id)) {
                printf(CLR_RED "Duplicate Book ID (BST).\n" CLR_RESET);
            } else {
                int err = 0;
                ctx->book_root = book_bst_insert(ctx->book_root, b, &err);
                if (err == 0) {
                    save_books(ctx);
                    printf(CLR_GREEN "Book added.\n" CLR_RESET);
                } else
                    printf(CLR_RED "Insert failed.\n" CLR_RESET);
            }
            pause_for_user();
        } else if (ch == 2) {
            print_separator();
            printf("Books (in-order BST by ID)\n");
            print_separator();
            if (!ctx->book_root)
                printf("(empty)\n");
            else
                print_books_by_id(ctx->book_root);
            pause_for_user();
        } else if (ch == 3) {
            TitleNode *tr = NULL;
            title_bst_build_from_booktree(&tr, ctx->book_root);
            print_separator();
            printf("Books sorted by title (temporary title BST)\n");
            print_separator();
            if (!tr)
                printf("(empty)\n");
            else
                title_bst_print_inorder(tr);
            title_bst_free(tr);
            pause_for_user();
        } else if (ch == 4) {
            printf("1) By ID  2) Title substring  3) Author substring\n");
            int sm;
            if (!read_int("Sub: ", &sm))
                continue;
            if (sm == 1) {
                int bid;
                if (read_int("Book ID: ", &bid)) {
                    BookNode *n = book_bst_search(ctx->book_root, bid);
                    if (!n)
                        printf(CLR_RED "Not found.\n" CLR_RESET);
                    else
                        printf("  ID %d | %s | %s | avail %d\n", n->data.book_id, n->data.title,
                               n->data.author, n->data.quantity - n->data.issued_count);
                }
            } else if (sm == 2) {
                char q[TITLE_LEN];
                read_line("Title contains: ", q, sizeof(q));
                book_search_by_title(ctx->book_root, q);
            } else if (sm == 3) {
                char q[AUTHOR_LEN];
                read_line("Author contains: ", q, sizeof(q));
                book_search_by_author(ctx->book_root, q);
            }
            pause_for_user();
        } else if (ch == 5) {
            int bid;
            if (read_int("Book ID: ", &bid)) {
                BookNode *n = book_bst_search(ctx->book_root, bid);
                if (!n)
                    printf(CLR_RED "Not found.\n" CLR_RESET);
                else {
                    read_line("New title (empty to keep): ", n->data.title, sizeof(n->data.title));
                    read_line("New author (empty to keep): ", n->data.author, sizeof(n->data.author));
                    int nq;
                    printf("New quantity (or -1 to keep): ");
                    if (scanf("%d", &nq) == 1) {
                        flush_line();
                        if (nq >= 0) {
                            if (nq < n->data.issued_count)
                                printf(CLR_RED "Quantity cannot be less than issued_count.\n" CLR_RESET);
                            else
                                n->data.quantity = nq;
                        }
                    } else
                        flush_line();
                    save_books(ctx);
                    printf(CLR_GREEN "Updated.\n" CLR_RESET);
                }
            }
            pause_for_user();
        } else if (ch == 6) {
            int bid, found = 0;
            if (read_int("Book ID to delete: ", &bid)) {
                BookNode *n = book_bst_search(ctx->book_root, bid);
                if (!n)
                    printf(CLR_RED "Not found.\n" CLR_RESET);
                else if (n->data.issued_count > 0)
                    printf(CLR_RED "Cannot delete — copies still issued.\n" CLR_RESET);
                else {
                    ctx->book_root = book_bst_delete(ctx->book_root, bid, &found);
                    if (found) {
                        save_books(ctx);
                        printf(CLR_GREEN "Deleted.\n" CLR_RESET);
                    }
                }
            }
            pause_for_user();
        } else if (ch == 7) {
            print_separator();
            printf("Active issues (FIFO queue order)\n");
            print_separator();
            int any = 0;
            for (IssueQNode *p = ctx->issue_front; p; p = p->next) {
                any = 1;
                printf("  stu %-5d | book %-5d | issued ", p->rec.student_id, p->rec.book_id);
                print_date(p->rec.issue_date);
                printf(" | due ");
                print_date(p->rec.due_date);
                printf("\n");
            }
            if (!any)
                printf("(none)\n");
            pause_for_user();
        } else if (ch == 8) {
            Student s;
            memset(&s, 0, sizeof(s));
            read_int("Student ID: ", &s.student_id);
            read_line("Username: ", s.username, sizeof(s.username));
            read_line("Full name: ", s.name, sizeof(s.name));
            read_line("Password: ", s.password, sizeof(s.password));
            if (student_bst_search_id(ctx->student_root, s.student_id))
                printf(CLR_RED "Duplicate student ID.\n" CLR_RESET);
            else {
                int err = 0;
                ctx->student_root = student_bst_insert(ctx->student_root, s, &err);
                if (err == 0) {
                    save_students(ctx);
                    printf(CLR_GREEN "Student added.\n" CLR_RESET);
                }
            }
            pause_for_user();
        } else if (ch == 9) {
            int sid, found = 0;
            if (read_int("Student ID to remove: ", &sid)) {
                int has = 0;
                for (IssueQNode *p = ctx->issue_front; p; p = p->next) {
                    if (p->rec.student_id == sid) {
                        has = 1;
                        break;
                    }
                }
                if (has)
                    printf(CLR_RED "Student has active loans.\n" CLR_RESET);
                else {
                    ctx->student_root = student_bst_delete(ctx->student_root, sid, &found);
                    if (found) {
                        save_students(ctx);
                        printf(CLR_GREEN "Removed.\n" CLR_RESET);
                    } else
                        printf(CLR_RED "Not found.\n" CLR_RESET);
                }
            }
            pause_for_user();
        } else if (ch == 10) {
            print_separator();
            printf("Students (BST in-order)\n");
            print_separator();
            if (!ctx->student_root)
                printf("(empty)\n");
            else
                print_students_inorder(ctx->student_root);
            pause_for_user();
        } else if (ch == 11) {
            print_separator();
            printf("Waitlist (FIFO)\n");
            print_separator();
            if (!ctx->wait_front)
                printf("(empty)\n");
            else {
                for (WaitQNode *w = ctx->wait_front; w; w = w->next)
                    printf("  student %-5d waiting for book %-5d\n", w->student_id, w->book_id);
            }
            pause_for_user();
        }
    }
}

static void student_menu(LibraryCtx *ctx, StudentNode *stu) {
    for (;;) {
        clear_screen();
        print_separator();
        printf(CLR_BOLD CLR_CYAN " STUDENT: %s (%d)\n" CLR_RESET, stu->data.name, stu->data.student_id);
        print_separator();
        printf("1) View all books (by ID)\n2) Search book\n3) Issue book\n4) Return book\n");
        printf("5) My issued books\n0) Logout\n");
        print_separator();
        int ch;
        if (!read_int("Choice: ", &ch))
            continue;
        if (ch == 0)
            break;
        if (ch == 1) {
            print_separator();
            print_books_by_id(ctx->book_root);
            pause_for_user();
        } else if (ch == 2) {
            printf("1) ID  2) Title  3) Author\n");
            int sm;
            if (!read_int("Sub: ", &sm))
                continue;
            if (sm == 1) {
                int bid;
                if (read_int("Book ID: ", &bid)) {
                    BookNode *n = book_bst_search(ctx->book_root, bid);
                    if (!n)
                        printf(CLR_RED "Not found.\n" CLR_RESET);
                    else
                        printf("  %s by %s | avail %d\n", n->data.title, n->data.author,
                               n->data.quantity - n->data.issued_count);
                }
            } else if (sm == 2) {
                char q[TITLE_LEN];
                read_line("Title contains: ", q, sizeof(q));
                book_search_by_title(ctx->book_root, q);
            } else if (sm == 3) {
                char q[AUTHOR_LEN];
                read_line("Author contains: ", q, sizeof(q));
                book_search_by_author(ctx->book_root, q);
            }
            pause_for_user();
        } else if (ch == 3) {
            int bid;
            if (read_int("Book ID to borrow: ", &bid))
                issue_book(ctx, stu->data.student_id, bid);
            pause_for_user();
        } else if (ch == 4) {
            int bid;
            if (read_int("Book ID to return: ", &bid))
                return_book(ctx, stu->data.student_id, bid);
            pause_for_user();
        } else if (ch == 5) {
            print_separator();
            printf("My loans (scan issue queue)\n");
            print_separator();
            for (IssueQNode *p = ctx->issue_front; p; p = p->next) {
                if (p->rec.student_id != stu->data.student_id)
                    continue;
                BookNode *bn = book_bst_search(ctx->book_root, p->rec.book_id);
                printf("  Book %-5d ", p->rec.book_id);
                if (bn)
                    printf("| %s | due ", bn->data.title);
                else
                    printf("| (unknown title) | due ");
                print_date(p->rec.due_date);
                printf("\n");
            }
            pause_for_user();
        }
    }
}

static void bootstrap_files(LibraryCtx *ctx) {
    FILE *af = fopen(ADMIN_FILE, "rb");
    if (!af) {
        seed_admin();
        printf(CLR_GREEN "Created default admin: admin / admin123\n" CLR_RESET);
    } else
        fclose(af);

    FILE *bf = fopen(BOOK_FILE, "rb");
    FILE *sf = fopen(STUDENT_FILE, "rb");
    int need_seed = 0;
    if (bf) {
        fseek(bf, 0, SEEK_END);
        if (ftell(bf) == 0)
            need_seed = 1;
        fclose(bf);
    } else
        need_seed = 1;

    if (sf) {
        fseek(sf, 0, SEEK_END);
        if (ftell(sf) == 0)
            need_seed = 1;
        fclose(sf);
    } else
        need_seed = 1;

    if (need_seed && !ctx->book_root && !ctx->student_root) {
        seed_books(ctx);
        seed_students(ctx);
        save_books(ctx);
        save_students(ctx);
        printf(CLR_GREEN "Seeded sample books and students (alice/bob/carol — passwords in source comments).\n" CLR_RESET);
    }
}

int main(void) {
    enable_ansi();
    LibraryCtx ctx;
    memset(&ctx, 0, sizeof(ctx));

    if (!load_books(&ctx) || !load_students(&ctx) || !load_issues(&ctx) || !load_waitlist(&ctx)) {
        fprintf(stderr, "Failed to load data files.\n");
        return 1;
    }

    bootstrap_files(&ctx);
    /* Reload if bootstrap created files */
    if (!ctx.book_root || !ctx.student_root) {
        book_bst_free(ctx.book_root);
        student_bst_free(ctx.student_root);
        issue_queue_clear(&ctx);
        wait_queue_clear(&ctx);
        ctx.book_root = NULL;
        ctx.student_root = NULL;
        load_books(&ctx);
        load_students(&ctx);
        load_issues(&ctx);
        load_waitlist(&ctx);
    }

    for (;;) {
        clear_screen();
        print_separator();
        printf(CLR_BOLD " LIBRARY MANAGEMENT SYSTEM\n" CLR_RESET);
        print_separator();
        printf(" 1) Admin login\n 2) Student login\n 3) Exit\n");
        print_separator();
        int m;
        if (!read_int("Choice: ", &m))
            continue;

        if (m == 3) {
            save_books(&ctx);
            save_students(&ctx);
            save_issues(&ctx);
            save_waitlist(&ctx);
            break;
        }
        if (m == 1) {
            if (admin_login())
                admin_menu(&ctx);
        } else if (m == 2) {
            char pass[PASS_LEN];
            StudentNode *sn = student_login(&ctx);
            if (!sn)
                printf(CLR_RED "Student not found.\n" CLR_RESET);
            else {
                read_line("Password: ", pass, sizeof(pass));
                if (strcmp(pass, sn->data.password) != 0)
                    printf(CLR_RED "Wrong password.\n" CLR_RESET);
                else
                    student_menu(&ctx, sn);
            }
            pause_for_user();
        }
    }

    wait_queue_clear(&ctx);
    book_bst_free(ctx.book_root);
    student_bst_free(ctx.student_root);
    issue_queue_clear(&ctx);
    printf("Goodbye.\n");
    return 0;
}
