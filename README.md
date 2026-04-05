# Library Management System (CLI)

A single-file **C** command-line library app for colleges or small libraries. It uses **binary search trees** for books and students and **FIFO queues** for active loans and waitlists. All data is stored in **binary** `.dat` files in the **current working directory** (run the program from the folder where you want those files).

## Requirements

- **GCC** (MinGW, MSYS2, or WSL on Windows; or `gcc` on Linux/macOS)
- Standard C library only in portable code; on Windows the program uses `windows.h` only to enable ANSI colors in the console.

## Build

```bash
gcc library.c -o library -std=c99 -Wall -Wextra
```

- **Windows:** produces `library.exe` if you name the output `library.exe`:
  ```bash
  gcc library.c -o library.exe -std=c99 -Wall -Wextra
  ```

## Run

From the directory that should hold the data files:

```bash
./library          # Linux / macOS / Git Bash
library.exe        # Windows CMD or PowerShell
```

If you run from another folder, `books.dat`, `students.dat`, etc. will be created **there** instead.

## Default accounts

| Role | Login | Password |
|------|--------|----------|
| **Admin** | `admin` | `admin123` |

Created automatically in `admin.dat` the first time the program runs if `admin.dat` is missing.

### Sample students (auto-seeded)

If both `books.dat` and `students.dat` are missing or empty, the program seeds sample books and students:

| Student ID | Username | Password |
|------------|----------|----------|
| 1001 | `alice` | `alice123` |
| 1002 | `bob` | `bob123` |
| 1003 | `carol` | `carol123` |

Students can log in with **ID** or **username**, then enter their password.

## Data files (binary)

| File | Purpose |
|------|---------|
| `admin.dat` | Admin username and password |
| `books.dat` | Book records (written in BST in-order) |
| `students.dat` | Student records (BST in-order) |
| `issues.dat` | Active loans (queue order) |
| `history.dat` | Returned loans (append-only history, fines) |
| `waitlist.dat` | Students waiting for a copy when stock is zero |

**Resetting the library:** exit the program, then delete the `.dat` files you want to recreate. The next run will recreate admin defaults and, if both catalog and student files are empty, seed sample data again.

## Features

### Admin

- Add / view / search / update / delete books  
- View books sorted by ID (BST in-order) or by title (temporary title BST)  
- View active issues, manage students, view waitlist  
- Secure login via `admin.dat`  

### Student

- View and search books  
- Issue and return books  
- View own loans  

### Business rules

- **Unique** book IDs and student IDs  
- At most **3** active loans per student  
- Cannot borrow the **same book** twice while one copy is still on loan  
- **Loan length:** 14 days (see `LOAN_DAYS` in `library.c`)  
- **Late return:** fine = days after due date × `FINE_PER_DAY` (default 5 units per day)  
- **Waitlist:** if no copy is available, the student is queued; when a copy is returned, eligible waiters are served in **FIFO** order  

## Configuration (source)

Edit the macros near the top of `library.c`:

- `MAX_BOOKS_PER_STUDENT`, `LOAN_DAYS`, `FINE_PER_DAY`  
- File names (`BOOK_FILE`, `STUDENT_FILE`, etc.)  

## Project layout

```
Library Management System/
├── library.c    # complete program (single file)
├── README.md
└── *.dat        # created at runtime when you run the executable
```

## License

Use and modify freely for learning or local deployment; no license is specified by the author unless you add one.
