// Library Management System (C++17)
// Build: g++ -std=c++17 library.cpp -o library      Run: ./library   (Windows: .\library.exe)
//
// Manages books, members and borrowing records with OOP and file handling.
// Features: add books, register members, issue / return (with late fines),
// search by title or author, view loans and overdue books. Data is saved to disk.

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------- settings
namespace config {
constexpr int LOAN_DAYS = 14;             // how long a book may be kept
constexpr int MAX_BOOKS_PER_MEMBER = 3;   // active loans allowed per member
constexpr int FINE_PER_DAY = 2;           // Rs. per overdue day
constexpr long long SECONDS_PER_DAY = 86400;
}  // namespace config

// ---------------------------------------------------------------- helpers
namespace util {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, delim)) parts.push_back(part);
    return parts;
}

bool toInt(const std::string& s, long long& out) {
    try {
        size_t used = 0;
        out = std::stoll(s, &used);
        return used == s.size();
    } catch (...) {
        return false;
    }
}

std::string formatDate(std::time_t t) {
    char buf[16];
    std::tm* tm = std::localtime(&t);
    std::strftime(buf, sizeof buf, "%Y-%m-%d", tm);
    return buf;
}

std::string clip(const std::string& s, size_t width) {
    return s.size() <= width ? s : s.substr(0, width - 3) + "...";
}

// Text fields are stored '|' separated, so '|' is not allowed inside them.
bool validText(const std::string& s) {
    return !s.empty() && s.size() <= 80 && s.find('|') == std::string::npos;
}

// Write to a temp file and rename, so a crash can't leave a half-written file.
bool writeLines(const std::string& path, const std::vector<std::string>& lines) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        for (const auto& l : lines) out << l << '\n';
        out.flush();
        if (!out) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    return !ec;
}

}  // namespace util

// ---------------------------------------------------------------- Book
class Book {
public:
    Book(int id, std::string title, std::string author, int total, int available)
        : id_(id), title_(std::move(title)), author_(std::move(author)),
          total_(total), available_(available) {}

    int id() const { return id_; }
    const std::string& title() const { return title_; }
    const std::string& author() const { return author_; }
    int total() const { return total_; }
    int available() const { return available_; }
    bool inStock() const { return available_ > 0; }

    void addCopies(int n) { total_ += n; available_ += n; }
    void checkOut() { --available_; }
    void checkIn() { ++available_; }
    void setAvailable(int n) { available_ = n; }

    std::string serialize() const {
        std::ostringstream o;
        o << id_ << '|' << title_ << '|' << author_ << '|' << total_ << '|' << available_;
        return o.str();
    }

    static std::optional<Book> parse(const std::string& line) {
        auto f = util::split(line, '|');
        long long id, total, avail;
        if (f.size() != 5 || !util::toInt(f[0], id) || !util::toInt(f[3], total) ||
            !util::toInt(f[4], avail) || total < 0 || avail < 0 || avail > total)
            return std::nullopt;
        return Book(static_cast<int>(id), f[1], f[2], static_cast<int>(total),
                    static_cast<int>(avail));
    }

private:
    int id_;
    std::string title_, author_;
    int total_, available_;
};

// ---------------------------------------------------------------- Member
class Member {
public:
    Member(int id, std::string name) : id_(id), name_(std::move(name)) {}
    int id() const { return id_; }
    const std::string& name() const { return name_; }

    std::string serialize() const { return std::to_string(id_) + '|' + name_; }

    static std::optional<Member> parse(const std::string& line) {
        auto f = util::split(line, '|');
        long long id;
        if (f.size() != 2 || !util::toInt(f[0], id)) return std::nullopt;
        return Member(static_cast<int>(id), f[1]);
    }

private:
    int id_;
    std::string name_;
};

// ---------------------------------------------------------------- Record
// One borrowing event. returned == 0 means the book is still out.
struct Record {
    int id = 0, bookId = 0, memberId = 0;
    std::time_t issued = 0, due = 0, returned = 0;
    int fine = 0;

    bool active() const { return returned == 0; }

    std::string serialize() const {
        std::ostringstream o;
        o << id << '|' << bookId << '|' << memberId << '|' << issued << '|' << due << '|'
          << returned << '|' << fine;
        return o.str();
    }

    static std::optional<Record> parse(const std::string& line) {
        auto f = util::split(line, '|');
        long long v[7];
        if (f.size() != 7) return std::nullopt;
        for (int i = 0; i < 7; ++i)
            if (!util::toInt(f[i], v[i])) return std::nullopt;
        Record r;
        r.id = static_cast<int>(v[0]);
        r.bookId = static_cast<int>(v[1]);
        r.memberId = static_cast<int>(v[2]);
        r.issued = static_cast<std::time_t>(v[3]);
        r.due = static_cast<std::time_t>(v[4]);
        r.returned = static_cast<std::time_t>(v[5]);
        r.fine = static_cast<int>(v[6]);
        return r;
    }
};

// ---------------------------------------------------------------- Library
enum class SearchBy { Title, Author, Either };

class Library {
public:
    explicit Library(std::string dir) : dir_(std::move(dir)) {}

    // Returns the number of unreadable lines that were skipped.
    int load() {
        int skipped = 0;
        for (const auto& l : readLines("books.dat")) {
            auto b = Book::parse(l);
            if (!b || books_.count(b->id())) { ++skipped; continue; }
            nextBookId_ = std::max(nextBookId_, b->id() + 1);
            books_.emplace(b->id(), *b);
        }
        for (const auto& l : readLines("members.dat")) {
            auto m = Member::parse(l);
            if (!m || members_.count(m->id())) { ++skipped; continue; }
            nextMemberId_ = std::max(nextMemberId_, m->id() + 1);
            members_.emplace(m->id(), *m);
        }
        for (const auto& l : readLines("records.dat")) {
            auto r = Record::parse(l);
            if (!r) { ++skipped; continue; }
            nextRecordId_ = std::max(nextRecordId_, r->id + 1);
            records_.push_back(*r);
        }
        // Keep stock counts consistent with the loans that are actually open.
        for (auto& [id, book] : books_) {
            int out = 0;
            for (const auto& r : records_)
                if (r.active() && r.bookId == id) ++out;
            book.setAvailable(std::max(0, book.total() - out));
        }
        return skipped;
    }

    bool save() const {
        std::error_code ec;
        std::filesystem::create_directories(dir_, ec);
        std::vector<std::string> b, m, r;
        for (const auto& [id, x] : books_) b.push_back(x.serialize());
        for (const auto& [id, x] : members_) m.push_back(x.serialize());
        for (const auto& x : records_) r.push_back(x.serialize());
        return util::writeLines(path("books.dat"), b) &&
               util::writeLines(path("members.dat"), m) &&
               util::writeLines(path("records.dat"), r);
    }

    // Adding a title/author that already exists just adds more copies.
    const Book* addBook(const std::string& title, const std::string& author, int copies,
                        std::string& err) {
        if (!util::validText(title) || !util::validText(author)) {
            err = "Title and author must be 1-80 characters and must not contain '|'.";
            return nullptr;
        }
        if (copies < 1 || copies > 1000) {
            err = "Number of copies must be between 1 and 1000.";
            return nullptr;
        }
        for (auto& [id, b] : books_) {
            if (util::lower(b.title()) == util::lower(title) &&
                util::lower(b.author()) == util::lower(author)) {
                b.addCopies(copies);
                return &b;
            }
        }
        const int id = nextBookId_++;
        return &books_.emplace(id, Book(id, title, author, copies, copies)).first->second;
    }

    const Member* addMember(const std::string& name, std::string& err) {
        if (!util::validText(name)) {
            err = "Name must be 1-80 characters and must not contain '|'.";
            return nullptr;
        }
        const int id = nextMemberId_++;
        return &members_.emplace(id, Member(id, name)).first->second;
    }

    // Case-insensitive "contains" search.
    std::vector<const Book*> search(const std::string& query, SearchBy by) const {
        std::vector<const Book*> found;
        const std::string q = util::lower(util::trim(query));
        if (q.empty()) return found;
        for (const auto& [id, b] : books_) {
            const bool t = util::lower(b.title()).find(q) != std::string::npos;
            const bool a = util::lower(b.author()).find(q) != std::string::npos;
            if ((by == SearchBy::Title && t) || (by == SearchBy::Author && a) ||
                (by == SearchBy::Either && (t || a)))
                found.push_back(&b);
        }
        return found;
    }

    bool issueBook(int bookId, int memberId, std::time_t now, std::string& err,
                   std::time_t* dueOut = nullptr) {
        auto bi = books_.find(bookId);
        if (bi == books_.end()) { err = "No book with that ID."; return false; }
        if (!members_.count(memberId)) { err = "No member with that ID."; return false; }
        if (!bi->second.inStock()) {
            err = "All copies of this book are currently issued.";
            return false;
        }
        int active = 0;
        for (const auto& r : records_) {
            if (!r.active() || r.memberId != memberId) continue;
            if (r.bookId == bookId) {
                err = "This member already has a copy of this book.";
                return false;
            }
            ++active;
        }
        if (active >= config::MAX_BOOKS_PER_MEMBER) {
            err = "Member has reached the limit of " +
                  std::to_string(config::MAX_BOOKS_PER_MEMBER) + " books.";
            return false;
        }
        Record r;
        r.id = nextRecordId_++;
        r.bookId = bookId;
        r.memberId = memberId;
        r.issued = now;
        r.due = now + config::LOAN_DAYS * config::SECONDS_PER_DAY;
        records_.push_back(r);
        bi->second.checkOut();
        if (dueOut) *dueOut = r.due;
        return true;
    }

    bool returnBook(int bookId, int memberId, std::time_t now, std::string& err,
                    int* fineOut = nullptr) {
        for (auto& r : records_) {
            if (!r.active() || r.bookId != bookId || r.memberId != memberId) continue;
            r.returned = now;
            r.fine = 0;
            if (now > r.due) {
                const long long days =
                    (now - r.due + config::SECONDS_PER_DAY - 1) / config::SECONDS_PER_DAY;
                r.fine = static_cast<int>(days) * config::FINE_PER_DAY;
            }
            books_.at(bookId).checkIn();
            if (fineOut) *fineOut = r.fine;
            return true;
        }
        err = "No active loan of that book for that member.";
        return false;
    }

    std::vector<const Record*> loansOf(int memberId) const {
        std::vector<const Record*> out;
        for (const auto& r : records_)
            if (r.active() && r.memberId == memberId) out.push_back(&r);
        return out;
    }

    std::vector<const Record*> overdue(std::time_t now) const {
        std::vector<const Record*> out;
        for (const auto& r : records_)
            if (r.active() && now > r.due) out.push_back(&r);
        return out;
    }

    const Book* findBook(int id) const {
        auto it = books_.find(id);
        return it == books_.end() ? nullptr : &it->second;
    }
    const Member* findMember(int id) const {
        auto it = members_.find(id);
        return it == members_.end() ? nullptr : &it->second;
    }
    std::vector<const Book*> allBooks() const {
        std::vector<const Book*> v;
        for (const auto& [id, b] : books_) v.push_back(&b);
        return v;
    }
    std::vector<const Member*> allMembers() const {
        std::vector<const Member*> v;
        for (const auto& [id, m] : members_) v.push_back(&m);
        return v;
    }

private:
    std::string path(const std::string& file) const {
        return (std::filesystem::path(dir_) / file).string();
    }
    std::vector<std::string> readLines(const std::string& file) const {
        std::vector<std::string> lines;
        std::ifstream in(path(file));
        std::string l;
        while (std::getline(in, l))
            if (!util::trim(l).empty()) lines.push_back(l);
        return lines;
    }

    std::string dir_;
    std::map<int, Book> books_;
    std::map<int, Member> members_;
    std::vector<Record> records_;
    int nextBookId_ = 1, nextMemberId_ = 1, nextRecordId_ = 1;
};

// ---------------------------------------------------------------- console UI
#ifndef LIBRARY_NO_MAIN
namespace ui {

bool readLine(const std::string& prompt, std::string& out) {
    std::cout << prompt << std::flush;
    if (!std::getline(std::cin, out)) return false;
    out = util::trim(out);
    return true;
}

// Returns false only when input has ended. Sets `out` to -1 if the text isn't a number.
bool readInt(const std::string& prompt, int& out) {
    std::string s;
    if (!readLine(prompt, s)) return false;
    long long v;
    out = (util::toInt(s, v) && v >= 0 && v <= 1000000) ? static_cast<int>(v) : -1;
    if (out < 0) std::cout << "  Please enter a valid whole number.\n";
    return true;
}

void printBooks(const std::vector<const Book*>& books) {
    if (books.empty()) { std::cout << "  No books found.\n"; return; }
    std::cout << "\n  " << std::left << std::setw(5) << "ID" << std::setw(32) << "Title"
              << std::setw(24) << "Author" << "Available\n  " << std::string(70, '-') << "\n";
    for (const Book* b : books)
        std::cout << "  " << std::left << std::setw(5) << b->id() << std::setw(32)
                  << util::clip(b->title(), 30) << std::setw(24) << util::clip(b->author(), 22)
                  << b->available() << " / " << b->total() << "\n";
}

void printLoans(const Library& lib, const std::vector<const Record*>& loans, std::time_t now) {
    if (loans.empty()) { std::cout << "  None.\n"; return; }
    std::cout << "\n  " << std::left << std::setw(5) << "Book" << std::setw(32) << "Title"
              << std::setw(8) << "Member" << std::setw(13) << "Due" << "Status\n  "
              << std::string(70, '-') << "\n";
    for (const Record* r : loans) {
        const Book* b = lib.findBook(r->bookId);
        std::string status = "On time";
        if (now > r->due) {
            const long long days = (now - r->due + config::SECONDS_PER_DAY - 1) /
                                   config::SECONDS_PER_DAY;
            status = "OVERDUE " + std::to_string(days) + "d (fine Rs. " +
                     std::to_string(days * config::FINE_PER_DAY) + ")";
        }
        std::cout << "  " << std::left << std::setw(5) << r->bookId << std::setw(32)
                  << util::clip(b ? b->title() : "?", 30) << std::setw(8) << r->memberId
                  << std::setw(13) << util::formatDate(r->due) << status << "\n";
    }
}

void save(const Library& lib) {
    if (!lib.save()) std::cout << "  WARNING: could not write to the data files!\n";
}

void addBook(Library& lib) {
    std::string title, author, err;
    int copies;
    std::cout << "\n--- Add a book ---\n";
    if (!readLine("Title: ", title) || !readLine("Author: ", author)) return;
    if (!readInt("Number of copies: ", copies) || copies < 0) return;
    const Book* b = lib.addBook(title, author, copies, err);
    if (!b) { std::cout << "  " << err << "\n"; return; }
    save(lib);
    std::cout << "  Saved. Book ID " << b->id() << " now has " << b->total() << " copies.\n";
}

void addMember(Library& lib) {
    std::string name, err;
    std::cout << "\n--- Register a member ---\n";
    if (!readLine("Member name: ", name)) return;
    const Member* m = lib.addMember(name, err);
    if (!m) { std::cout << "  " << err << "\n"; return; }
    save(lib);
    std::cout << "  Registered " << m->name() << ". Member ID is " << m->id() << ".\n";
}

void search(const Library& lib) {
    std::string mode, q;
    std::cout << "\n--- Search books ---\n 1. By title\n 2. By author\n 3. Title or author\n";
    if (!readLine("Choice: ", mode)) return;
    if (mode != "1" && mode != "2" && mode != "3") { std::cout << "  Please pick 1-3.\n"; return; }
    const SearchBy by = mode == "1" ? SearchBy::Title
                      : mode == "2" ? SearchBy::Author : SearchBy::Either;
    if (!readLine("Search text: ", q)) return;
    printBooks(lib.search(q, by));
}

void issue(Library& lib) {
    int bookId, memberId;
    std::string err;
    std::cout << "\n--- Issue a book ---\n";
    if (!readInt("Book ID: ", bookId) || bookId < 0) return;
    if (!readInt("Member ID: ", memberId) || memberId < 0) return;
    std::time_t due = 0;
    if (!lib.issueBook(bookId, memberId, std::time(nullptr), err, &due)) {
        std::cout << "  Cannot issue: " << err << "\n";
        return;
    }
    save(lib);
    std::cout << "  Issued \"" << lib.findBook(bookId)->title() << "\" to "
              << lib.findMember(memberId)->name() << ". Due on " << util::formatDate(due) << ".\n";
}

void giveBack(Library& lib) {
    int bookId, memberId, fine = 0;
    std::string err;
    std::cout << "\n--- Return a book ---\n";
    if (!readInt("Book ID: ", bookId) || bookId < 0) return;
    if (!readInt("Member ID: ", memberId) || memberId < 0) return;
    if (!lib.returnBook(bookId, memberId, std::time(nullptr), err, &fine)) {
        std::cout << "  Cannot return: " << err << "\n";
        return;
    }
    save(lib);
    std::cout << "  Book returned.";
    if (fine > 0) std::cout << " Late fine to collect: Rs. " << fine << ".";
    std::cout << "\n";
}

void memberLoans(const Library& lib) {
    int id;
    std::cout << "\n--- Books borrowed by a member ---\n";
    if (!readInt("Member ID: ", id) || id < 0) return;
    const Member* m = lib.findMember(id);
    if (!m) { std::cout << "  No member with that ID.\n"; return; }
    std::cout << "  " << m->name() << " currently has:\n";
    printLoans(lib, lib.loansOf(id), std::time(nullptr));
}

void listMembers(const Library& lib) {
    auto members = lib.allMembers();
    if (members.empty()) { std::cout << "  No members registered.\n"; return; }
    std::cout << "\n  " << std::left << std::setw(5) << "ID" << "Name\n  " << std::string(40, '-') << "\n";
    for (const Member* m : members)
        std::cout << "  " << std::left << std::setw(5) << m->id() << m->name() << "\n";
}

}  // namespace ui

int main(int argc, char** argv) {
    Library lib(argc > 1 ? argv[1] : "data");
    const int skipped = lib.load();
    if (skipped > 0) std::cout << "Note: skipped " << skipped << " unreadable record(s).\n";

    std::cout << "=========================================\n"
              << "     LIBRARY MANAGEMENT SYSTEM (C++)\n"
              << "=========================================\n";
    std::string choice;
    while (true) {
        std::cout << "\n 1. Add book\n 2. Register member\n 3. Search books (title/author)\n"
                  << " 4. List all books\n 5. List members\n 6. Issue book\n 7. Return book\n"
                  << " 8. Books borrowed by a member\n 9. Show overdue books\n 0. Exit\n";
        if (!ui::readLine("Choice: ", choice) || choice == "0") break;
        if (choice == "1") ui::addBook(lib);
        else if (choice == "2") ui::addMember(lib);
        else if (choice == "3") ui::search(lib);
        else if (choice == "4") ui::printBooks(lib.allBooks());
        else if (choice == "5") ui::listMembers(lib);
        else if (choice == "6") ui::issue(lib);
        else if (choice == "7") ui::giveBack(lib);
        else if (choice == "8") ui::memberLoans(lib);
        else if (choice == "9") ui::printLoans(lib, lib.overdue(std::time(nullptr)), std::time(nullptr));
        else std::cout << "  Please pick 0-9.\n";
    }
    std::cout << "Goodbye!\n";
    return 0;
}
#endif  // LIBRARY_NO_MAIN
