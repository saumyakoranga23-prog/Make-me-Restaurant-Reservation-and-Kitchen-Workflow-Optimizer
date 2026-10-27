#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_TABLES 12
#define NAME_LEN 64
#define PHONE_LEN 24
#define DISH_LEN 64
#define DATA_FILE "restaurant_data.txt"

typedef struct {
    int id;
    int capacity;
    int occupied;
} Table;

typedef struct Reservation {
    int id;
    char customer[NAME_LEN];
    char phone[PHONE_LEN];
    int guests;
    char datetime[32];
    int table_id;
    struct Reservation *next;
} Reservation;

typedef enum {
    ORDER_PENDING = 0,
    ORDER_PREPARING,
    ORDER_READY,
    ORDER_SERVED
} OrderStatus;

typedef struct Order {
    int id;
    int reservation_id;
    char dish[DISH_LEN];
    int prep_minutes;
    int priority; /* Higher number means more urgent. */
    OrderStatus status;
    time_t created_at;
    struct Order *next;
} Order;

typedef struct WaitEntry {
    int id;
    char customer[NAME_LEN];
    int guests;
    char datetime[32];
    struct WaitEntry *next;
} WaitEntry;

static Table tables[MAX_TABLES] = {
    {1, 2, 0}, {2, 2, 0}, {3, 2, 0}, {4, 4, 0},
    {5, 4, 0}, {6, 4, 0}, {7, 6, 0}, {8, 6, 0},
    {9, 8, 0}, {10, 8, 0}, {11, 4, 0}, {12, 2, 0}
};
static Reservation *reservations = NULL;
static Order *orders = NULL;
static WaitEntry *waitlist = NULL;
static int next_reservation_id = 1;
static int next_order_id = 1;
static int next_wait_id = 1;

static void read_line(const char *prompt, char *buffer, size_t size) {
    printf("%s", prompt);
    if (fgets(buffer, (int)size, stdin) == NULL) {
        buffer[0] = '\0';
        return;
    }
    buffer[strcspn(buffer, "\n")] = '\0';
}

static int read_int(const char *prompt, int min_value, int max_value) {
    char buffer[64], *end;
    long value;
    for (;;) {
        read_line(prompt, buffer, sizeof(buffer));
        value = strtol(buffer, &end, 10);
        if (buffer[0] != '\0' && *end == '\0' &&
            value >= min_value && value <= max_value) return (int)value;
        printf("Enter a whole number from %d to %d.\n", min_value, max_value);
    }
}

static void trim_copy(char *dest, size_t size, const char *src) {
    snprintf(dest, size, "%s", src);
    dest[strcspn(dest, "\n")] = '\0';
}

static Table *find_table(int id) {
    for (int i = 0; i < MAX_TABLES; i++)
        if (tables[i].id == id) return &tables[i];
    return NULL;
}

static Reservation *find_reservation(int id) {
    for (Reservation *r = reservations; r; r = r->next)
        if (r->id == id) return r;
    return NULL;
}

static Order *find_order(int id) {
    for (Order *o = orders; o; o = o->next)
        if (o->id == id) return o;
    return NULL;
}

/* Greedy table assignment: choose the smallest free table that fits the party. */
static Table *allocate_table(int guests) {
    Table *best = NULL;
    for (int i = 0; i < MAX_TABLES; i++) {
        if (!tables[i].occupied && tables[i].capacity >= guests &&
            (!best || tables[i].capacity < best->capacity))
            best = &tables[i];
    }
    return best;
}

static void add_waitlist(const char *name, int guests, const char *datetime) {
    WaitEntry *w = malloc(sizeof(*w));
    if (!w) { puts("Memory allocation failed."); return; }
    w->id = next_wait_id++;
    trim_copy(w->customer, sizeof(w->customer), name);
    w->guests = guests;
    trim_copy(w->datetime, sizeof(w->datetime), datetime);
    w->next = NULL;
    if (!waitlist) waitlist = w;
    else {
        WaitEntry *tail = waitlist;
        while (tail->next) tail = tail->next;
        tail->next = w;
    }
    printf("No suitable table is free. Added to waiting list with ID %d.\n", w->id);
}

static void book_reservation(void) {
    char name[NAME_LEN], phone[PHONE_LEN], datetime[32];
    read_line("Customer name: ", name, sizeof(name));
    if (!name[0]) { puts("Name cannot be empty."); return; }
    read_line("Phone number: ", phone, sizeof(phone));
    int guests = read_int("Number of guests (1-8): ", 1, 8);
    read_line("Date/time (e.g. 2026-11-02 19:30): ", datetime, sizeof(datetime));
    if (!datetime[0]) snprintf(datetime, sizeof(datetime), "Not specified");

    Table *table = allocate_table(guests);
    if (!table) {
        add_waitlist(name, guests, datetime);
        return;
    }

    Reservation *r = malloc(sizeof(*r));
    if (!r) { puts("Memory allocation failed."); return; }
    r->id = next_reservation_id++;
    trim_copy(r->customer, sizeof(r->customer), name);
    trim_copy(r->phone, sizeof(r->phone), phone);
    r->guests = guests;
    trim_copy(r->datetime, sizeof(r->datetime), datetime);
    r->table_id = table->id;
    r->next = reservations;
    reservations = r;
    table->occupied = 1;
    printf("Reservation confirmed. ID: %d | Table: %d (capacity %d)\n",
           r->id, table->id, table->capacity);
}

static void list_reservations(void) {
    if (!reservations) { puts("No active reservations."); return; }
    puts("\nID   Customer                         Guests Table Date/time");
    puts("--------------------------------------------------------------------------");
    for (Reservation *r = reservations; r; r = r->next)
        printf("%-4d %-32s %-6d %-5d %s\n", r->id, r->customer,
               r->guests, r->table_id, r->datetime);
}

static void cancel_reservation(void) {
    int id = read_int("Reservation ID to cancel: ", 1, 1000000);
    Reservation **link = &reservations;
    while (*link && (*link)->id != id) link = &(*link)->next;
    if (!*link) { puts("Reservation not found."); return; }
    Reservation *r = *link;
    *link = r->next;
    Table *t = find_table(r->table_id);
    if (t) t->occupied = 0;
    printf("Cancelled reservation %d for %s; table %d is free.\n",
           r->id, r->customer, r->table_id);
    free(r);
    /* Move the first waiting party that fits into the newly freed table. */
    WaitEntry **wlink = &waitlist;
    while (*wlink) {
        WaitEntry *w = *wlink;
        if (t && w->guests <= t->capacity) {
            Reservation *nr = malloc(sizeof(*nr));
            if (!nr) { puts("Could not allocate a reservation for waiting guest."); return; }
            nr->id = next_reservation_id++;
            snprintf(nr->customer, sizeof(nr->customer), "%s", w->customer);
            snprintf(nr->phone, sizeof(nr->phone), "N/A");
            nr->guests = w->guests;
            snprintf(nr->datetime, sizeof(nr->datetime), "%s", w->datetime);
            nr->table_id = t->id;
            nr->next = reservations;
            reservations = nr;
            t->occupied = 1;
            *wlink = w->next;
            printf("Waiting-list guest %s promoted to reservation %d at table %d.\n",
                   w->customer, nr->id, t->id);
            free(w);
            break;
        }
        wlink = &(*wlink)->next;
    }
}

static void list_waitlist(void) {
    if (!waitlist) { puts("Waiting list is empty."); return; }
    puts("\nQueue position | ID | Customer | Guests | Date/time");
    int pos = 1;
    for (WaitEntry *w = waitlist; w; w = w->next, pos++)
        printf("%-14d | %-2d | %-20s | %-6d | %s\n",
               pos, w->id, w->customer, w->guests, w->datetime);
}

static const char *status_name(OrderStatus status) {
    switch (status) {
        case ORDER_PENDING: return "Pending";
        case ORDER_PREPARING: return "Preparing";
        case ORDER_READY: return "Ready";
        case ORDER_SERVED: return "Served";
        default: return "Unknown";
    }
}

static void add_order(void) {
    int reservation_id = read_int("Reservation ID (0 if walk-in): ", 0, 1000000);
    if (reservation_id != 0 && !find_reservation(reservation_id)) {
        puts("Reservation not found. Use 0 for a walk-in order.");
        return;
    }
    char dish[DISH_LEN];
    read_line("Dish name: ", dish, sizeof(dish));
    if (!dish[0]) { puts("Dish name cannot be empty."); return; }
    int prep = read_int("Estimated preparation time in minutes (1-240): ", 1, 240);
    int priority = read_int("Priority (1=normal, 2=high, 3=urgent): ", 1, 3);

    Order *o = malloc(sizeof(*o));
    if (!o) { puts("Memory allocation failed."); return; }
    o->id = next_order_id++;
    o->reservation_id = reservation_id;
    trim_copy(o->dish, sizeof(o->dish), dish);
    o->prep_minutes = prep;
    o->priority = priority;
    o->status = ORDER_PENDING;
    o->created_at = time(NULL);
    o->next = orders;
    orders = o;
    printf("Order added. Order ID: %d\n", o->id);
}

static void list_orders(void) {
    if (!orders) { puts("No kitchen orders."); return; }
    puts("\nID   ResID Dish                             Prep Priority Status");
    puts("-----------------------------------------------------------------------");
    for (Order *o = orders; o; o = o->next)
        printf("%-4d %-5d %-32s %-4d %-8d %s\n", o->id, o->reservation_id,
               o->dish, o->prep_minutes, o->priority, status_name(o->status));
}

/* Greedy kitchen scheduler: priority first, then shortest prep time, then oldest order. */
static void show_kitchen_schedule(void) {
    int count = 0;
    for (Order *o = orders; o; o = o->next)
        if (o->status == ORDER_PENDING) count++;
    if (!count) { puts("No pending orders to schedule."); return; }

    Order **items = malloc((size_t)count * sizeof(*items));
    if (!items) { puts("Memory allocation failed."); return; }
    int i = 0;
    for (Order *o = orders; o; o = o->next)
        if (o->status == ORDER_PENDING) items[i++] = o;

    /* Insertion sort: small kitchen queues do not need a complicated dependency. */
    for (i = 1; i < count; i++) {
        Order *key = items[i];
        int j = i - 1;
        while (j >= 0 &&
              (items[j]->priority < key->priority ||
               (items[j]->priority == key->priority &&
                items[j]->prep_minutes > key->prep_minutes) ||
               (items[j]->priority == key->priority &&
                items[j]->prep_minutes == key->prep_minutes &&
                items[j]->created_at > key->created_at))) {
            items[j + 1] = items[j];
            j--;
        }
        items[j + 1] = key;
    }

    puts("\nOptimized pending-order schedule");
    puts("Sequence | Order | Dish | Priority | Prep | Estimated cumulative minutes");
    int cumulative = 0;
    for (i = 0; i < count; i++) {
        cumulative += items[i]->prep_minutes;
        printf("%-8d | %-5d | %-24s | %-8d | %-4d | %d\n",
               i + 1, items[i]->id, items[i]->dish, items[i]->priority,
               items[i]->prep_minutes, cumulative);
    }
    printf("Total sequential kitchen workload: %d minutes\n", cumulative);
    puts("Note: estimates assume one preparation station and no parallel cooking.");
    free(items);
}

static void update_order_status(void) {
    int id = read_int("Order ID: ", 1, 1000000);
    Order *o = find_order(id);
    if (!o) { puts("Order not found."); return; }
    printf("Current status: %s\n", status_name(o->status));
    puts("1. Pending  2. Preparing  3. Ready  4. Served");
    int s = read_int("New status: ", 1, 4);
    o->status = (OrderStatus)(s - 1);
    printf("Order %d updated to %s.\n", o->id, status_name(o->status));
}

static void show_tables(void) {
    puts("\nTable | Capacity | Status");
    puts("--------------------------");
    int occupied = 0, seats = 0, occupied_seats = 0;
    for (int i = 0; i < MAX_TABLES; i++) {
        printf("%-5d | %-8d | %s\n", tables[i].id, tables[i].capacity,
               tables[i].occupied ? "Occupied/reserved" : "Available");
        seats += tables[i].capacity;
        if (tables[i].occupied) { occupied++; occupied_seats += tables[i].capacity; }
    }
    printf("Tables occupied: %d/%d | Seat-capacity utilization: %.1f%%\n",
           occupied, MAX_TABLES, seats ? 100.0 * occupied_seats / seats : 0.0);
}

static void show_reports(void) {
    int reservation_count = 0, guest_count = 0, order_count = 0;
    int pending = 0, preparing = 0, ready = 0, served = 0;
    for (Reservation *r = reservations; r; r = r->next) {
        reservation_count++;
        guest_count += r->guests;
    }
    for (Order *o = orders; o; o = o->next) {
        order_count++;
        if (o->status == ORDER_PENDING) pending++;
        else if (o->status == ORDER_PREPARING) preparing++;
        else if (o->status == ORDER_READY) ready++;
        else served++;
    }
    int occupied = 0;
    for (int i = 0; i < MAX_TABLES; i++) occupied += tables[i].occupied;
    printf("\n========== RESTAURANT REPORT ==========\n");
    printf("Active reservations : %d\n", reservation_count);
    printf("Guests reserved     : %d\n", guest_count);
    printf("Tables occupied     : %d/%d (%.1f%%)\n", occupied, MAX_TABLES,
           100.0 * occupied / MAX_TABLES);
    printf("Waiting-list parties: ");
    int waiting = 0;
    for (WaitEntry *w = waitlist; w; w = w->next) waiting++;
    printf("%d\n", waiting);
    printf("Total kitchen orders: %d\n", order_count);
    printf("Pending: %d | Preparing: %d | Ready: %d | Served: %d\n",
           pending, preparing, ready, served);
    printf("=======================================\n");
}

static void save_data(void) {
    FILE *fp = fopen(DATA_FILE, "w");
    if (!fp) { perror("Could not save data"); return; }
    for (int i = 0; i < MAX_TABLES; i++)
        fprintf(fp, "T %d %d %d\n", tables[i].id, tables[i].capacity, tables[i].occupied);
    for (Reservation *r = reservations; r; r = r->next)
        fprintf(fp, "R %d|%s|%s|%d|%s|%d\n", r->id, r->customer, r->phone,
                r->guests, r->datetime, r->table_id);
    for (Order *o = orders; o; o = o->next)
        fprintf(fp, "O %d|%d|%s|%d|%d|%d|%lld\n", o->id, o->reservation_id,
                o->dish, o->prep_minutes, o->priority, (int)o->status,
                (long long)o->created_at);
    for (WaitEntry *w = waitlist; w; w = w->next)
        fprintf(fp, "W %d|%s|%d|%s\n", w->id, w->customer, w->guests, w->datetime);
    fclose(fp);
    printf("Data saved to %s\n", DATA_FILE);
}

static void load_data(void) {
    FILE *fp = fopen(DATA_FILE, "r");
    if (!fp) return; /* First run is normal. */
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == 'T') {
            int id, cap, occupied;
            if (sscanf(line, "T %d %d %d", &id, &cap, &occupied) == 3) {
                Table *t = find_table(id);
                if (t) t->occupied = occupied;
            }
        } else if (line[0] == 'R') {
            Reservation *r = calloc(1, sizeof(*r));
            if (!r) break;
            int n = sscanf(line, "R %d|%63[^|]|%23[^|]|%d|%31[^|]|%d",
                           &r->id, r->customer, r->phone, &r->guests,
                           r->datetime, &r->table_id);
            if (n == 6) {
                r->next = reservations; reservations = r;
                if (r->id >= next_reservation_id) next_reservation_id = r->id + 1;
            } else free(r);
        } else if (line[0] == 'O') {
            Order *o = calloc(1, sizeof(*o));
            if (!o) break;
            long long created;
            int n = sscanf(line, "O %d|%d|%63[^|]|%d|%d|%d|%lld",
                           &o->id, &o->reservation_id, o->dish, &o->prep_minutes,
                           &o->priority, (int *)&o->status, &created);
            if (n == 7) {
                o->created_at = (time_t)created;
                o->next = orders; orders = o;
                if (o->id >= next_order_id) next_order_id = o->id + 1;
            } else free(o);
        } else if (line[0] == 'W') {
            WaitEntry *w = calloc(1, sizeof(*w));
            if (!w) break;
            int n = sscanf(line, "W %d|%63[^|]|%d|%31[^\n]",
                           &w->id, w->customer, &w->guests, w->datetime);
            if (n == 4) {
                w->next = NULL;
                if (!waitlist) waitlist = w;
                else { WaitEntry *tail = waitlist; while (tail->next) tail = tail->next; tail->next = w; }
                if (w->id >= next_wait_id) next_wait_id = w->id + 1;
            } else free(w);
        }
    }
    fclose(fp);
}

static void free_all(void) {
    while (reservations) { Reservation *n = reservations->next; free(reservations); reservations = n; }
    while (orders) { Order *n = orders->next; free(orders); orders = n; }
    while (waitlist) { WaitEntry *n = waitlist->next; free(waitlist); waitlist = n; }
}

static void menu(void) {
    puts("\n==============================================");
    puts(" RESTAURANT RESERVATION & KITCHEN OPTIMIZER");
    puts("==============================================");
    puts("1. Book reservation");
    puts("2. List reservations");
    puts("3. Cancel reservation");
    puts("4. Show tables");
    puts("5. Show waiting list");
    puts("6. Add kitchen order");
    puts("7. List kitchen orders");
    puts("8. Generate optimized kitchen schedule");
    puts("9. Update order status");
    puts("10. View reports");
    puts("11. Save data");
    puts("0. Save and exit");
}

int main(void) {
    load_data();
    puts("Restaurant workflow system started.");
    for (;;) {
        menu();
        int choice = read_int("Choose an option: ", 0, 11);
        switch (choice) {
            case 1: book_reservation(); break;
            case 2: list_reservations(); break;
            case 3: cancel_reservation(); break;
            case 4: show_tables(); break;
            case 5: list_waitlist(); break;
            case 6: add_order(); break;
            case 7: list_orders(); break;
            case 8: show_kitchen_schedule(); break;
            case 9: update_order_status(); break;
            case 10: show_reports(); break;
            case 11: save_data(); break;
            case 0: save_data(); free_all(); puts("Goodbye."); return 0;
        }
    }
}
