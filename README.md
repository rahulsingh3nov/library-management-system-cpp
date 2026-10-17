# Library Management System (C++)

A console-based Library Management System written in C++17. It manages **books, members and borrowing records**, supports **issuing and returning books with late fines**, and lets staff **search by title or author**. All data is saved to disk, so records persist between runs.

## Features

- Add books (adding an existing title/author increases its copy count instead of creating a duplicate)
- Register library members
- **Search** books by title, by author, or either (case-insensitive, partial matches work)
- **Issue** and **return** books with stock tracking per book
- Borrowing rules enforced automatically:

  | Rule | Value |
  |---|---|
  | Loan period | 14 days |
  | Maximum books per member | 3 |
  | Late fine | Rs. 2 per overdue day |
  | Same book twice to one member | Not allowed |
  | Issuing when no copies are left | Not allowed |

- View the books a member currently holds, and a list of all overdue books
- Persistent storage in plain-text files, with crash-safe saves and tolerance for corrupted lines

## OOP design

| Class | Responsibility |
|---|---|
| `Book` | Title, author, total and available copies; serialisation |
| `Member` | Member ID and name; serialisation |
| `Record` | One borrowing event: issue date, due date, return date, fine |
| `Library` | Owns all data, enforces the rules, handles search, loading and saving |
| `ui` namespace | Console menus and input validation, kept separate from the logic |

The console code never changes data directly. It always goes through `Library`, which keeps the business rules in one place and makes them testable.

## Build and run

Requires a C++17 compiler (g++ or clang++).

```bash
g++ -std=c++17 library.cpp -o library
./library            # Windows: .\library.exe
```

Data is stored in a `data/` folder next to where you run the program. To use another folder: `./library mydata`.

## Run the tests

```bash
g++ -std=c++17 -I. tests/test_library.cpp -o test_library
./test_library       # Windows: .\test_library.exe
```

The tests cover adding and merging books, all three search modes, every issue rule, fine calculation (using fixed dates so results are repeatable), persistence across restarts, and recovery from corrupted data.

## Project structure

```
library-management-system/
├── library.cpp              # whole application
├── tests/
│   └── test_library.cpp     # automated tests
├── .gitignore
└── README.md
```

## Data files

Created in `data/`, one record per line, `|` separated:

```
books.dat     id|title|author|totalCopies|availableCopies
members.dat   id|name
records.dat   id|bookId|memberId|issuedTime|dueTime|returnedTime|fine
```

## Possible extensions

Member search, book removal, reservations, fine payment tracking, a login for librarian accounts, a database back end.

## Author

Your Name · [GitHub](https://github.com/your-username) · [LinkedIn](https://linkedin.com/in/your-profile)
