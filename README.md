# Restaurant Reservation and Kitchen Workflow Optimizer (C)

A console-based C project for managing restaurant reservations, tables, a waiting list, kitchen orders, kitchen scheduling, and summary reports.

## Features
- Greedy allocation of the smallest available table that can seat a party.
- Reservation creation and cancellation.
- FIFO waiting list; a suitable waiting party is promoted when a table is freed.
- Kitchen order tracking with Pending, Preparing, Ready, and Served states.
- Kitchen schedule sorted by urgency, then shortest preparation time, then oldest order.
- Reports for reservations, guest counts, table utilization, and order statuses.
- File persistence in `restaurant_data.txt`.

## Build and run

### Linux (Fedora/Ubuntu)
```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -O2 main.c -o restaurant_optimizer
./restaurant_optimizer
```

### Windows (MinGW)
```powershell
gcc -std=c11 -Wall -Wextra -Wpedantic -O2 main.c -o restaurant_optimizer.exe
.\restaurant_optimizer.exe
```

## DSA concepts
- **Linked lists:** reservations, orders, and waiting-list entries.
- **Queue behavior:** waiting list uses FIFO order, while skipping parties that cannot fit the newly freed table.
- **Greedy algorithm:** table allocation minimizes unused seats for the current party.
- **Insertion sort:** pending kitchen orders are ordered by priority, preparation time, and creation time.
- **File handling:** records are saved on exit and can be loaded on the next launch.

## Important assumptions
- This is a single-process, console-based prototype, not a production point-of-sale system.
- Table occupancy is treated as a current reservation allocation, not a time-slot calendar. The program does not detect overlapping booking times.
- Kitchen estimates assume one sequential preparation station. Real kitchens prepare multiple dishes in parallel.
- Data fields use `|` as a delimiter, so names and dish names should not contain that character.
- Data is saved in the current working directory.
