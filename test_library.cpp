// Dependency-free tests.
// Build & run from the project folder:
//   g++ -std=c++17 -I. tests/test_library.cpp -o test_library && ./test_library
#define LIBRARY_NO_MAIN
#include "../library.cpp"

static int failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::cerr << "FAIL line " << __LINE__ << ": " #cond << "\n"; \
            ++failures;                                                  \
        }                                                                \
    } while (0)

int main() {
    const std::string dir = "test_library_tmp";
    std::filesystem::remove_all(dir);
    const std::time_t t0 = 1700000000;  // a fixed "today" so tests are repeatable
    const long long DAY = config::SECONDS_PER_DAY;
    std::string err;

    {
        Library lib(dir);
        CHECK(lib.load() == 0);

        // Adding books
        const Book* b1 = lib.addBook("The Alchemist", "Paulo Coelho", 2, err);
        const Book* b2 = lib.addBook("Wings of Fire", "A. P. J. Abdul Kalam", 1, err);
        CHECK(b1 && b1->id() == 1 && b2 && b2->id() == 2);
        CHECK(lib.addBook("the alchemist", "PAULO COELHO", 1, err)->total() == 3);  // merged
        CHECK(lib.allBooks().size() == 2);
        CHECK(!lib.addBook("", "x", 1, err));
        CHECK(!lib.addBook("Bad|Title", "x", 1, err));
        CHECK(!lib.addBook("T", "A", 0, err));

        // Members
        CHECK(lib.addMember("Rahul", err)->id() == 1);
        CHECK(lib.addMember("Priya", err)->id() == 2);
        CHECK(!lib.addMember("Bad|Name", err));

        // Search by title / author / either, case-insensitive
        CHECK(lib.search("alchem", SearchBy::Title).size() == 1);
        CHECK(lib.search("alchem", SearchBy::Author).empty());
        CHECK(lib.search("KALAM", SearchBy::Author).size() == 1);
        CHECK(lib.search("kalam", SearchBy::Title).empty());
        CHECK(lib.search("o", SearchBy::Either).size() == 2);
        CHECK(lib.search("   ", SearchBy::Either).empty());
        CHECK(lib.search("nothing", SearchBy::Either).empty());

        // Issue rules
        std::time_t due = 0;
        CHECK(lib.issueBook(2, 1, t0, err, &due));
        CHECK(due == t0 + 14 * DAY);
        CHECK(lib.findBook(2)->available() == 0);
        CHECK(!lib.issueBook(2, 2, t0, err));       // no copies left
        CHECK(!lib.issueBook(1, 99, t0, err));      // unknown member
        CHECK(!lib.issueBook(99, 1, t0, err));      // unknown book
        CHECK(lib.issueBook(1, 1, t0, err));
        CHECK(!lib.issueBook(1, 1, t0, err));       // same book twice
        lib.addBook("Book Three", "Author", 1, err);
        lib.addBook("Book Four", "Author", 1, err);
        CHECK(lib.issueBook(3, 1, t0, err));        // member 1 now has 3 books
        CHECK(!lib.issueBook(4, 1, t0, err));       // limit reached
        CHECK(lib.loansOf(1).size() == 3);

        // Overdue + fines
        CHECK(lib.overdue(t0 + 14 * DAY).empty());
        CHECK(lib.overdue(t0 + 15 * DAY).size() == 3);
        int fine = -1;
        CHECK(lib.returnBook(2, 1, t0 + 10 * DAY, err, &fine) && fine == 0);   // on time
        CHECK(lib.findBook(2)->available() == 1);
        CHECK(lib.returnBook(1, 1, t0 + 19 * DAY, err, &fine) && fine == 5 * 2);  // 5 days late
        CHECK(!lib.returnBook(1, 1, t0 + 19 * DAY, err));  // already returned
        CHECK(!lib.returnBook(4, 2, t0, err));             // never issued
        CHECK(lib.loansOf(1).size() == 1);
        CHECK(lib.save());
    }
    {
        // Persistence: everything survives a restart
        Library lib(dir);
        CHECK(lib.load() == 0);
        CHECK(lib.allBooks().size() == 4);
        CHECK(lib.allMembers().size() == 2);
        CHECK(lib.findBook(1)->total() == 3 && lib.findBook(1)->available() == 3);
        CHECK(lib.findBook(3)->available() == 0);   // still on loan to member 1
        CHECK(lib.loansOf(1).size() == 1);
        CHECK(lib.addMember("Third", err)->id() == 3);  // ids keep counting up
        std::string e2;
        CHECK(lib.issueBook(2, 2, t0, e2));
    }
    {
        // Corrupt lines are skipped, not fatal
        std::ofstream out(dir + "/books.dat", std::ios::app);
        out << "garbage\n9|X|Y|1|5\n";   // available > total is invalid
    }
    {
        Library lib(dir);
        CHECK(lib.load() == 2);
        CHECK(lib.allBooks().size() == 4);
    }

    std::filesystem::remove_all(dir);
    if (failures == 0) std::cout << "All tests passed.\n";
    return failures == 0 ? 0 : 1;
}
