#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <limits> // Required for numeric_limits
#include <algorithm>
#include <regex>
#include <cstdio> // Required for remove() and rename()
#include <stdexcept> // For stoi exceptions

using namespace std;

// --- Constants for Filenames ---
const string USERS_FILE = "users.txt";
const string ITEMS_FILE = "items.txt";
const string SUPPLIERS_FILE = "suppliers.txt";
const string SALES_FILE = "sales.txt";
const string REQUISITIONS_FILE = "purchase_requisitions.txt";
const string ORDERS_FILE = "purchase_orders.txt";

// --- Helper Functions ---

// Helper to safely read an integer from input
int getValidatedIntegerInput(const string& prompt) {
    int value;
    string inputLine;
    while (true) {
        cout << prompt;
        getline(cin, inputLine);
        stringstream ss(inputLine);
        // Check if read integer AND consumed the whole line (no extra chars like '12a')
        if (ss >> value && ss.eof()) {
            // Optional: Add range checks if needed (e.g., value > 0)
            // if (value > 0) { // Example check
                 break; // Valid input
            // } else {
            //     cout << "Invalid input. Please enter a positive whole number." << endl;
            // }
        } else {
            cerr << "Invalid input. Please enter a whole number." << endl;
        }
    }
    return value;
}

// Helper for basic YYYY-MM-DD format and range validation
bool isDateValid(const string& date) {
    regex pattern(R"(\d{4}-\d{2}-\d{2})"); // YYYY-MM-DD
    if (!regex_match(date, pattern)) {
        cerr << "Error: Date format must be YYYY-MM-DD." << endl;
        return false;
    }
    try {
        int year = stoi(date.substr(0, 4));
        int month = stoi(date.substr(5, 2));
        int day = stoi(date.substr(8, 2));

        // Basic range checks (can be made more sophisticated for days in month/leap year)
        if (year < 1900 || year > 2100) return false;
        if (month < 1 || month > 12) return false;
        if (day < 1 || day > 31) return false; // Simplistic day check

        // Add more specific checks (e.g., days in month, leap year for Feb)
        if ((month == 4 || month == 6 || month == 9 || month == 11) && day > 30) return false;
        if (month == 2) {
            bool isLeap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
            if (isLeap && day > 29) return false;
            if (!isLeap && day > 28) return false;
        }
        return true;
    } catch (const invalid_argument& ia) {
        cerr << "Error: Invalid date components (not numbers)." << endl;
        return false;
    } catch (const out_of_range& oor) {
        cerr << "Error: Date components out of range." << endl;
        return false;
    }
    return false; // Should not reach here ideally
}


// --- Base Class ---
class FileManager {
public:
    // Check if the first word (ID/Code) on any line matches a value
    bool doesIdExist(const string& idValue, const string& filename) {
        ifstream file(filename);
        if (!file.is_open()) {
            cerr << "Error: Cannot open file for checking existence: " << filename << endl;
            return false; // Treat as non-existent if file can't be opened? Or throw?
        }

        string line;
        bool found = false;
        while (getline(file, line)) {
            if (line.empty()) continue;
            istringstream iss(line);
            string firstWord;
            if (iss >> firstWord) {
                if (idValue == firstWord) {
                    found = true;
                    break;
                }
            } else {
                 if (file.fail() && !file.eof() && !file.bad()) {
                     cerr << "Warning: Error reading line during existence check in " << filename << endl;
                     // Decide how to handle - continue or return error?
                 }
            }
        }
        file.close();
        return found;
    }

    // Delete an entry by its ID (first word on the line) - uses safe write
    bool deleteEntryById(const string& idToDelete, const string& filename) {
        vector<string> entries;
        ifstream infile(filename);
        if (!infile.is_open()) {
            cerr << "Error: Cannot open file for deletion: " << filename << endl;
            return false;
        }

        string line;
        bool entryFound = false;
        bool readError = false;
        while (getline(infile, line)) {
             string currentId;
             istringstream iss(line);
             if (iss >> currentId) {
                 if (currentId == idToDelete) {
                     entryFound = true;
                     // Don't add this line to the vector
                 } else {
                     entries.push_back(line); // Keep other lines
                 }
             } else {
                 if (!line.empty()) { // Only report error if line wasn't just blank
                    cerr << "Warning: Could not parse ID from line during delete: " << line << endl;
                 }
                 // Decide if you want to keep potentially corrupt lines
                 entries.push_back(line);
             }
             if (infile.fail() && !infile.eof()) {
                 cerr << "Error: Failed reading from file during delete: " << filename << endl;
                 readError = true;
                 break; // Stop processing on read error
             }
        }
        infile.close();

        if (readError) {
            return false; // Don't proceed with write if read failed
        }

        if (!entryFound) {
            cout << "Entry with ID '" << idToDelete << "' not found for deletion." << endl;
            return false;
        }

        // --- Safe Write using Temporary File ---
        string tempFilename = filename + ".tmp";
        ofstream outfile(tempFilename);
        if (!outfile.is_open()) {
            cerr << "Error: Could not create temporary file for deletion: " << tempFilename << endl;
            return false;
        }

        bool writeError = false;
        for (const string& entry : entries) {
            outfile << entry << endl;
            if (outfile.fail()) {
                cerr << "Error: Failed writing to temporary file during delete: " << tempFilename << endl;
                writeError = true;
                break;
            }
        }
        outfile.close(); // Close explicitly before rename/remove

        if (writeError) {
            remove(tempFilename.c_str()); // Attempt to clean up temp file
            return false;
        }

        // If write succeeded, replace original with temp
        if (remove(filename.c_str()) != 0) {
            perror(("Error deleting original file " + filename).c_str());
            remove(tempFilename.c_str()); // Clean up temp file
            return false;
        }
        if (rename(tempFilename.c_str(), filename.c_str()) != 0) {
            perror(("Error renaming temporary file to " + filename).c_str());
            // Attempt to rename back? Difficult state.
            return false;
        }

        cout << "Entry with ID '" << idToDelete << "' deleted successfully." << endl;
        return true;
    }

    // Helper to parse a line robustly, handling spaces in the 'name' field
    // Assumes format: ID Name Field3 Field4 ... LastField
    // Returns false if parsing fails significantly
    static bool parseLineRobustly(const string& line, vector<string>& fields) {
        fields.clear();
        if (line.empty()) return true; // Empty line is valid but yields no fields

        istringstream iss(line);
        string firstWord; // The ID/Code
        if (!(iss >> firstWord)) {
             cerr << "Warning: Could not parse first field (ID/Code) from line: " << line << endl;
             return false; // Cannot proceed without ID
        }
        fields.push_back(firstWord);

        string remainingPart;
        getline(iss, remainingPart); // Get the rest of the line

        // Trim leading space from remainingPart if present
        size_t firstChar = remainingPart.find_first_not_of(' ');
        if (string::npos != firstChar) {
            remainingPart = remainingPart.substr(firstChar);
        } else {
            // No more fields after the ID
            return true;
        }

        // --- This part needs customization based on the *exact* number of fields expected ---
        // Simple example: Assume Name is everything up to the last space, and the rest is the last field
        size_t lastSpace = remainingPart.find_last_of(' ');
        if (lastSpace == string::npos) {
            // Only one more field (e.g., Name only)
            fields.push_back(remainingPart);
        } else {
            // Separate Name and LastField
            fields.push_back(remainingPart.substr(0, lastSpace)); // Field 2 (Name)
            fields.push_back(remainingPart.substr(lastSpace + 1)); // Last Field
            // NOTE: This simple example assumes exactly 3 fields total (ID Name LastField)
            // You MUST adapt this logic if you have more fields (e.g., PRs, POs)
            // For more fields, you might need to split `remainingPart` differently,
            // potentially reading known numeric fields after the name.
        }
        return true;
    }
     // Specific parser for lines with potential spaces only in the second field (e.g., Item, Supplier)
    static bool parseNameInSecondField(const string& line, vector<string>& fields) {
        fields.clear();
        if (line.empty()) return true;

        istringstream iss(line);
        string field1, field3; // ID and Last Field

        if (!(iss >> field1)) {
            cerr << "Warning: Could not parse Field 1 (ID) from line: " << line << endl;
            return false;
        }
        fields.push_back(field1);

        // Attempt to read the last field first by seeking backwards or reading normally if simple
        // This example reads the rest and assumes the last "word" is the last field
        string intermediatePart, lastWord;
        string namePart;

        // Read words until the end, assuming the last word is the final field
        string currentWord;
        vector<string> words;
        while(iss >> currentWord) {
            words.push_back(currentWord);
        }

        if (words.empty()) {
             // Only ID was present
             namePart = "";
             lastWord = "";
        } else {
            lastWord = words.back();
            words.pop_back(); // Remove last word

            // Reconstruct name from remaining words
            stringstream name_ss;
            for(size_t i = 0; i < words.size(); ++i) {
                name_ss << words[i] << (i == words.size() - 1 ? "" : " ");
            }
            namePart = name_ss.str();
        }

        fields.push_back(namePart); // Field 2 (Name)
        fields.push_back(lastWord); // Field 3 (Last Field)

        return true;
    }

    static bool writeVectorToFile(const vector<string>& lines, const string& filename) {
        string tempFilename = filename + ".tmp";
        ofstream outfile(tempFilename);
        // Check if outfile is open...
        if (!outfile.is_open()) {
             cerr << "Error: Could not create temporary file: " << tempFilename << endl;
             return false;
        }
        bool writeError = false;
        for (const string& entry : lines) {
            outfile << entry << endl;
            if (outfile.fail()) {
                cerr << "Error: Failed writing to temporary file: " << tempFilename << endl;
                writeError = true;
                break;
             }
        }
        outfile.close();
        if (writeError) {
             remove(tempFilename.c_str()); // Attempt cleanup
             return false;
        }
        // Rename/remove logic
        if (remove(filename.c_str()) != 0) {
             perror(("Error deleting original file " + filename).c_str());
             remove(tempFilename.c_str()); // Attempt cleanup
             return false;
        }
        if (rename(tempFilename.c_str(), filename.c_str()) != 0) {
             perror(("Error renaming temporary file to " + filename).c_str());
             // Critical error state - maybe try renaming temp back?
             return false;
        }
        return true; // Indicate success
    }

    // NEW: Specific parser for Item lines (4 fields)
    static bool parseItemLine(const string& line, vector<string>& fields) {
        fields.clear();
        if (line.empty()) return true;

        istringstream iss(line);
        string field1, field3, field4; // Code, Supplier, Qty
        // We need to extract the name which might have spaces

        if (!(iss >> field1)) { /* ... Error handling ... */ return false; }
        fields.push_back(field1); // ItemCode

        // Read the rest, extract last two words (Supplier, Qty)
        string remainingPart;
        getline(iss, remainingPart);
        size_t firstChar = remainingPart.find_first_not_of(' ');
        if (string::npos == firstChar) { /* ... Error handling ... */ return false; } // Nothing after ID
        remainingPart = remainingPart.substr(firstChar);

        size_t lastSpace = remainingPart.find_last_of(' ');
        if (lastSpace == string::npos) { /* ... Error handling ... */ return false; } // Only Name? Missing Supplier/Qty
        field4 = remainingPart.substr(lastSpace + 1); // Potential Qty

        remainingPart = remainingPart.substr(0, lastSpace); // Part before Qty

        lastSpace = remainingPart.find_last_of(' ');
         if (lastSpace == string::npos) { // Only Name? Missing Supplier
             field3 = remainingPart; // Assume this is supplier? Or error?
             fields.push_back(""); // Empty Name
             fields.push_back(field3);
             fields.push_back(field4);
             cerr << "Warning: Could not properly parse Item Name/Supplier ID from line: " << line << endl;

         } else {
            fields.push_back(remainingPart.substr(0, lastSpace)); // Name
            fields.push_back(remainingPart.substr(lastSpace + 1)); // Supplier Code
            fields.push_back(field4); // Quantity
         }

        // Basic validation: Check if quantity field is numeric
        try {
            stoi(fields[3]);
        } catch (...) {
            cerr << "Warning: Non-numeric quantity found in items file line: " << line << endl;
            return false; // Treat as parsing error
        }


        return fields.size() == 4;
    }

};


// --- Data Classes ---

// Forward declaration for Application class methods
class Application;

// User class
class User : protected FileManager {
private:
    string userId; // Generated ID is the key
    string password;
    string accessLevel;

public:
    // Constructor used for creating object before registration
    User(string pwd, string accLevel) : password(pwd), accessLevel(accLevel) {}
    // Constructor used potentially later if loading users into objects
    User(string id, string pwd, string accLevel) : userId(id), password(pwd), accessLevel(accLevel) {}


    // Register a new user - Appends directly to file
    bool registerUser() {
        // Validate access level
        if (accessLevel != "SM" && accessLevel != "PM" && accessLevel != "Admin") {
            cerr << "Error: Invalid access level provided: " << accessLevel << ". Use SM/PM/Admin." << endl;
            return false;
        }
        if (password.empty()) {
             cerr << "Error: Password cannot be empty." << endl;
             return false;
        }

        // Generate unique user ID
        string newUserId = generateNewUserId(accessLevel);
        if (newUserId.empty()) {
             cerr << "Error: Could not generate a unique User ID." << endl;
             return false; // Error occurred during generation/check
        }
        this->userId = newUserId; // Store generated ID in the object

        // Save user details to file
        ofstream file(USERS_FILE, ios::app); // Append mode
        if (!file.is_open()) {
            cerr << "Error: Cannot open users file for registration: " << USERS_FILE << endl;
            return false;
        }

        file << this->userId << " " << this->password << " " << this->accessLevel << endl;

        if (file.fail()) {
            cerr << "Error: Failed to write user registration to file: " << USERS_FILE << endl;
            file.close();
            // Should we attempt to remove the potentially partial line? Difficult.
            return false;
        }
        file.close();
        cout << "User registration successful. User ID: " << this->userId << endl;
        return true;
    }

    // Login a user
    static bool login(const string& enteredUsername, const string& enteredPassword, string& grantedAccessLevel) {
        ifstream file(USERS_FILE);
         if (!file.is_open()) {
            cerr << "Error: Cannot open users file for login: " << USERS_FILE << endl;
            return false;
        }

        string line;
        bool loginSuccess = false;
        while (getline(file, line)) {
            if (line.empty()) continue;
            string savedUsername, savedPassword, savedAccessLevel;
            istringstream iss(line);

            if (iss >> savedUsername >> savedPassword >> savedAccessLevel) { // Simple parsing assuming no spaces in these fields
                 if (enteredUsername == savedUsername && enteredPassword == savedPassword) {
                    grantedAccessLevel = savedAccessLevel;
                    loginSuccess = true;
                    break; // Found match
                 }
            } else {
                 cerr << "Warning: Skipping malformed line in users file: " << line << endl;
            }
            if (file.fail() && !file.eof()) {
                 cerr << "Error: Failed reading from users file during login." << endl;
                 // Decide if we should abort login completely
                 // loginSuccess = false; // Ensure failure
                 // break;
            }
        }
        file.close();
        return loginSuccess;
    }

private:
    // Generate a unique user ID based on access level
    string generateNewUserId(const string& accLevel) {
        ifstream file(USERS_FILE);
         if (!file.is_open()) {
            cerr << "Error: Cannot open users file for ID generation: " << USERS_FILE << endl;
            return ""; // Return empty on error
        }

        string line;
        int maxId = 0;
        string prefix;

        if (accLevel == "SM") prefix = "SM";
        else if (accLevel == "PM") prefix = "PM";
        else if (accLevel == "Admin") prefix = "Admin";
        else return ""; // Invalid access level passed

        bool readError = false;
        while (getline(file, line)) {
            if (line.empty()) continue;
            istringstream iss(line);
            string existingUserId;
            if (iss >> existingUserId) {
                // Check if the ID starts with the correct prefix
                if (existingUserId.rfind(prefix, 0) == 0) { // Efficient check for prefix
                    string numPart = existingUserId.substr(prefix.length());
                    try {
                        int id = stoi(numPart);
                        if (id > maxId) {
                            maxId = id;
                        }
                    } catch (const invalid_argument& ia) {
                        cerr << "Warning: Invalid numeric part in User ID '" << existingUserId << "' in " << USERS_FILE << endl;
                    } catch (const out_of_range& oor) {
                         cerr << "Warning: User ID number out of range '" << existingUserId << "' in " << USERS_FILE << endl;
                    }
                }
            } else {
                 cerr << "Warning: Could not parse User ID from line: " << line << endl;
            }
             if (file.fail() && !file.eof()) {
                 cerr << "Error: Failed reading from users file during ID generation." << endl;
                 readError = true;
                 break;
            }
        }
        file.close();

        if(readError) return ""; // Return empty on error

        // Return the next ID
        return prefix + to_string(maxId + 1);
    }
};


// Item class
class Item : protected FileManager {
friend class Application; // Allow Application to call static methods
private:
    string itemCode;
    string itemName;
    string supplierID; // Should exist in suppliers.txt
    int quantityInStock; // Added for stock tracking

    public:
    // Constructor needs initial quantity
    Item(string code, string name, string supId, int initialQty) :
        itemCode(code), itemName(name), supplierID(supId), quantityInStock(initialQty) {}

    // Add an item - Validates and appends directly to file
    // Add an item - Now includes initial quantity
    bool addItem() {
        // Validation (includes quantity > 0 check)
        if (itemCode.empty() || itemName.empty() || supplierID.empty() || quantityInStock < 0) { // Allow 0 initial stock
            cerr << "Error: Item Code, Name, Supplier ID required, and Initial Quantity cannot be negative." << endl;
            return false;
        }
        if (doesIdExist(itemCode, ITEMS_FILE)) { /* ... Error handling ... */ return false; }
        if (!doesSupplierExist(supplierID)) { /* ... Error handling ... */ return false; }

        // Append to file including quantity
        ofstream file(ITEMS_FILE, ios::app);
        if (!file.is_open()) { /* ... Error handling ... */ return false; }

        // SAVE FORMAT: Code Name Supplier Qty
        file << itemCode << " " << itemName << " " << supplierID << " " << quantityInStock << endl;

        if (file.fail()) { /* ... Error handling ... */ file.close(); return false; }
        file.close();
        cout << "Item '" << itemCode << "' added successfully with initial stock " << quantityInStock << "." << endl;
        return true;
    }

    // List all items in the inventory
    static void listItems() {
        cout << "\n--- Item List ---" << endl;
        cout << "Code      Name                 Supplier" << endl;
        cout << "---------------------------------------" << endl;
        ifstream file(ITEMS_FILE);
        if (!file.is_open()) {
            cerr << "Error: Cannot open items file for listing: " << ITEMS_FILE << endl;
            return;
        }
        string line;
        int count = 0;
        while (getline(file, line)) {
            if (line.empty()) continue;
            vector<string> fields;
            // Use the parser that handles spaces in the second field (Name)
            if (FileManager::parseNameInSecondField(line, fields) && fields.size() == 3) {
                 // Basic formatting (adjust widths as needed)
                printf("%-9s %-20s %-10s\n", fields[0].c_str(), fields[1].c_str(), fields[2].c_str());
                count++;
            } else {
                cerr << "Warning: Skipping malformed line in items file: " << line << endl;
            }
             if (file.fail() && !file.eof()) {
                 cerr << "Error: Failed reading items file during listing." << endl;
                 break; // Stop listing on error
            }
        }
         if (count == 0) {
             cout << "(No items found)" << endl;
         }
        cout << "---------------------------------------" << endl;
        file.close();
    }

    // Edit an item - Reads/Writes quantity but doesn't allow editing it here
    static bool editItem(const string& itemCodeToEdit) {
        vector<string> fileLines;
        ifstream infile(ITEMS_FILE);
        if (!infile.is_open()) { /* ... Error handling ... */ return false; }
        string line;
        bool found = false;
        bool readError = false;
        int lineIndex = -1;
        string currentQuantityStr = "0"; // Store current quantity for rewriting

        while (getline(infile, line)) {
             fileLines.push_back(line);
             if (!found) {
                 vector<string> fields;
                 // Use new parser to check ID and get current quantity
                 if (FileManager::parseItemLine(line, fields) && fields.size() == 4) {
                     if (fields[0] == itemCodeToEdit) {
                         found = true;
                         lineIndex = fileLines.size() - 1;
                         currentQuantityStr = fields[3]; // Store qty from file
                     }
                 } else if (!line.empty()) {
                     cerr << "Warning: Malformed line encountered during edit search: " << line << endl;
                 }
             }
             if (infile.fail() && !infile.eof()) { /* ... Error handling ... */ readError = true; break; }
        }
        infile.close();
        if (readError || !found) { /* ... Error or not found message ... */ return false; }

        // Get new data (only Name and SupplierID - ItemCode & Qty are immutable here)
        string newItemName, newSupplierID;
        cout << "Editing Item Code: " << itemCodeToEdit << " (Current Stock: " << currentQuantityStr << ")" << endl;
        cout << "Enter new item name: "; getline(cin, newItemName);
        if (newItemName.empty()) { /* ... Error handling ... */ return false; }
        cout << "Enter new supplier ID: "; getline(cin, newSupplierID);
        if (!doesSupplierExist(newSupplierID)) { /* ... Error handling ... */ return false; }

        // Update the line in the vector, keeping original code and quantity
        fileLines[lineIndex] = itemCodeToEdit + " " + newItemName + " " + newSupplierID + " " + currentQuantityStr;

        // --- Safe Write using FileManager helper ---
        if (!FileManager::writeVectorToFile(fileLines, ITEMS_FILE)) return false;

        cout << "Item '" << itemCodeToEdit << "' updated successfully." << endl;
        return true;
    }

    // Delete an item
    static bool deleteItem(const string& itemCodeToDelete) {
        FileManager fm; // Use base class method
        return fm.deleteEntryById(itemCodeToDelete, ITEMS_FILE);
    }

    // NEW: Decrease stock quantity - uses safe write
    static bool decreaseStock(const string& itemCodeToDecrease, int quantityToDecrease) {
        if (quantityToDecrease <= 0) {
            cerr << "Error: Quantity to decrease must be positive." << endl;
            return false;
        }

        vector<string> fileLines;
        ifstream infile(ITEMS_FILE);
        if (!infile.is_open()) { /* ... Error handling ... */ return false; }

        string line;
        bool found = false;
        bool readError = false;
        int lineIndex = -1;
        int currentQuantity = -1;
        string lineToModify; // Store the full original line data for reconstruction

        while (getline(infile, line)) {
             fileLines.push_back(line);
             if (!found) {
                 vector<string> fields;
                 if (FileManager::parseItemLine(line, fields) && fields.size() == 4) {
                      if (fields[0] == itemCodeToDecrease) {
                         found = true;
                         lineIndex = fileLines.size() - 1;
                         try {
                             currentQuantity = stoi(fields[3]);
                         } catch (...) {
                              cerr << "Error: Invalid stock quantity found in file for item " << itemCodeToDecrease << endl;
                              readError = true; // Treat as read error
                         }
                         lineToModify = line; // Store the whole line data
                      }
                 } else if (!line.empty()) {
                     cerr << "Warning: Malformed line encountered during stock update search: " << line << endl;
                 }
             }
              if (infile.fail() && !infile.eof()) { /* ... Error handling ... */ readError = true; break; }
        }
        infile.close();

        if (readError) return false;
        if (!found || currentQuantity < 0) {
            if(!found) cerr << "Error: Item '" << itemCodeToDecrease << "' not found for stock update." << endl;
            return false;
        }

        // Check if stock is sufficient
        if (currentQuantity < quantityToDecrease) {
            cerr << "Error: Insufficient stock for item '" << itemCodeToDecrease
                 << "'. Required: " << quantityToDecrease << ", Available: " << currentQuantity << endl;
            return false;
        }

        // Calculate and update
        int newQuantity = currentQuantity - quantityToDecrease;

        // Reconstruct the line using parsed fields to be safe
        vector<string> fields;
        if (FileManager::parseItemLine(lineToModify, fields) && fields.size() == 4) {
             fileLines[lineIndex] = fields[0] + " " + fields[1] + " " + fields[2] + " " + to_string(newQuantity);
        } else {
             cerr << "Error: Failed to re-parse line during stock update. Aborting." << endl;
             return false; // Safety check
        }


        // --- Safe Write using FileManager helper ---
        if (!FileManager::writeVectorToFile(fileLines, ITEMS_FILE)) {
             cerr << "Error: Failed to write updated stock level to file." << endl;
             return false;
        }

        // cout << "Stock for item '" << itemCodeToDecrease << "' updated to " << newQuantity << "." << endl; // Optional success message
        return true; // Stock updated successfully
    }

     // NEW: Get Supplier for a given Item Code
     static string getSupplierForItem(const string& itemCode) {
          ifstream file(ITEMS_FILE);
          if (!file.is_open()) { /* Error */ return ""; }
          string line;
          string supplier = "";
          while(getline(file, line)) {
              vector<string> fields;
              if(FileManager::parseItemLine(line, fields) && fields.size() == 4) {
                  if (fields[0] == itemCode) {
                      supplier = fields[2]; // Supplier ID is the 3rd field (index 2)
                      break;
                  }
              }
               if (file.fail() && !file.eof()) { /* Error */ break; }
          }
          file.close();
          return supplier;
     }

private:
    // Helper validation function
    static bool doesSupplierExist(const string& supplierID) {
        FileManager fm;
        return fm.doesIdExist(supplierID, SUPPLIERS_FILE);
    }
};

// --- Supplier Class (Similar structure to Item) ---
class Supplier : protected FileManager {
friend class Application;
private:
    string supplierCode;
    string supplierName;
    // string itemID; // REMOVED: itemID field removed

public:
    // Constructor updated: Removed itId parameter
    Supplier(string code, string name) :
        supplierCode(code), supplierName(name) {}

    // Add a supplier - Validates and appends directly
    bool addSupplier() {
        // Validation updated: Removed itemID check
        if (supplierCode.empty() || supplierName.empty()) {
            cerr << "Error: Supplier Code and Name cannot be empty." << endl;
            return false;
        }
        if (doesIdExist(supplierCode, SUPPLIERS_FILE)) {
            cerr << "Error: Supplier code '" << supplierCode << "' already exists." << endl;
            return false;
        }
        // REMOVED: Optional validation for itemID

        ofstream file(SUPPLIERS_FILE, ios::app);
        if (!file.is_open()) {
            cerr << "Error: Cannot open suppliers file for adding: " << SUPPLIERS_FILE << endl;
            return false;
        }
        // File write updated: Only save code and name
        file << supplierCode << " " << supplierName << endl;
        if (file.fail()) {
            cerr << "Error: Failed writing new supplier to file: " << SUPPLIERS_FILE << endl;
            file.close();
            return false;
        }
        file.close();
        cout << "Supplier '" << supplierCode << "' added successfully." << endl;
        return true;
    }

    // List all suppliers - Updated parsing and header
    static void listSuppliers() {
        cout << "\n--- Supplier List ---" << endl;
        cout << "Code      Name" << endl; // Header updated
        cout << "----------------------------------------" << endl;
        ifstream file(SUPPLIERS_FILE);
        if (!file.is_open()) {
            cerr << "Error: Cannot open suppliers file for listing: " << SUPPLIERS_FILE << endl;
            return;
        }
        string line;
        int count = 0;
        while (getline(file, line)) {
            if (line.empty()) continue;
            vector<string> fields;
            // Use robust parser - expecting 2 fields now (Code, Name potentially with spaces)
            // We need a parser that handles potentially multi-word names
            // For simplicity, let's adapt parseLineRobustly or assume name is rest of line
            istringstream iss(line);
            string code, namePart;
            if (iss >> code) { // Read the code
                getline(iss, namePart); // Read the rest as name
                // Trim leading space from namePart if necessary
                size_t firstChar = namePart.find_first_not_of(' ');
                if (string::npos != firstChar) {
                    namePart = namePart.substr(firstChar);
                } else {
                    namePart = "[No Name Found]"; // Handle case where only code exists?
                }
                 // Simple formatting (adjust widths) - Updated fields
                printf("%-9s %-20s\n", code.c_str(), namePart.c_str());
                count++;
            } else {
                cerr << "Warning: Skipping malformed line in suppliers file: " << line << endl;
            }
            if (file.fail() && !file.eof()) {
                cerr << "Error: Failed reading suppliers file during listing." << endl;
                break;
            }
        }
        if (count == 0) {
            cout << "(No suppliers found)" << endl;
        }
        cout << "----------------------------------------" << endl;
        file.close();
    }

     // Edit a supplier - uses safe write, supplierCode is immutable
     static bool editSupplier(const string& supplierCodeToEdit) {
        vector<string> fileLines;
        ifstream infile(SUPPLIERS_FILE);
        if (!infile.is_open()) { /* ... Error handling ... */ return false; }
        string line;
        bool found = false;
        bool readError = false;
        int lineIndex = -1;
        int currentLineNum = 0; // Keep track of line number for index

        while (getline(infile, line)) {
            fileLines.push_back(line);
            if (!found) {
                // *** FIX: Added logic to find the line ***
                istringstream iss(line);
                string currentCode;
                if (iss >> currentCode) { // Parse the first word (ID)
                   if (currentCode == supplierCodeToEdit) {
                        found = true;
                        lineIndex = currentLineNum; // Found the line, store its index
                    }
                } else if (!line.empty()) {
                     cerr << "Warning: Could not parse supplier code from line during edit: " << line << endl;
                }
            }
            currentLineNum++; // Increment line number
             if (infile.fail() && !infile.eof()) { /* ... Error handling ... */ readError = true; break; }
        }
        infile.close();

        if (readError) { cerr << "Error reading supplier file during edit." << endl; return false;}
        if (!found) { cout << "Supplier with code '" << supplierCodeToEdit << "' not found for editing." << endl; return false; }

        // Get new data (Name only)
        string newSupplierName; // Removed newItemID
        cout << "Editing Supplier Code: " << supplierCodeToEdit << endl;
        cout << "Enter new supplier name: ";
        getline(cin, newSupplierName);
        if (newSupplierName.empty()) { cerr << "Error: Supplier name cannot be empty." << endl; return false; }
        // REMOVED: Input/Validation for Item ID

        // Update line in vector - Format updated
        fileLines[lineIndex] = supplierCodeToEdit + " " + newSupplierName;

        // --- Safe Write using FileManager helper ---
        if (!FileManager::writeVectorToFile(fileLines, SUPPLIERS_FILE)) {
            cerr << "Error writing updated supplier data to file." << endl;
             return false;
        }

        cout << "Supplier '" << supplierCodeToEdit << "' updated successfully." << endl;
        return true;
    }



   // Delete remains the same, uses FileManager::deleteEntryById
   static bool deleteSupplier(const string& supplierCodeToDelete) {
        FileManager fm;
        return fm.deleteEntryById(supplierCodeToDelete, SUPPLIERS_FILE);
    }

// private: // Keep private helpers if needed
    // Example helper if item validation is added
    // static bool doesItemExist(const string& itemCode) {
    //     FileManager fm;
    //     return fm.doesIdExist(itemCode, ITEMS_FILE);
    // }
};


// --- SalesEntry Class ---
class SalesEntry : protected FileManager {
friend class Application;
private:
    string saleID; // <<< NEW: Added Sale ID member
    string itemCode; // Should exist in items.txt
    int quantity;
    string salesDate; // Should be YYYY-MM-DD

    // --- NEW: Private helper to generate unique Sale ID ---
    static string generateNewSaleID() {
        ifstream file(SALES_FILE);
        if (!file.is_open()) {
            cerr << "Error: Cannot open sales file for ID generation: " << SALES_FILE << endl;
            return ""; // Return empty on error
        }

        string line;
        int maxId = 0;
        string prefix = "S"; // Example prefix for Sale IDs

        bool readError = false;
        while (getline(file, line)) {
            if (line.empty()) continue;
            istringstream iss(line);
            string existingSaleID;
            if (iss >> existingSaleID) { // Read the first field (Sale ID)
                // Check if the ID starts with the correct prefix
                if (existingSaleID.rfind(prefix, 0) == 0) { // Efficient check for prefix
                    string numPart = existingSaleID.substr(prefix.length());
                    try {
                        int id = stoi(numPart);
                        if (id > maxId) {
                            maxId = id;
                        }
                    } catch (const invalid_argument& ia) {
                         cerr << "Warning: Invalid numeric part in Sale ID '" << existingSaleID << "' in " << SALES_FILE << endl;
                    } catch (const out_of_range& oor) {
                          cerr << "Warning: Sale ID number out of range '" << existingSaleID << "' in " << SALES_FILE << endl;
                    }
                }
            } else {
                cerr << "Warning: Could not parse Sale ID from line: " << line << endl;
            }
            if (file.fail() && !file.eof()) {
                cerr << "Error: Failed reading from sales file during ID generation." << endl;
                readError = true;
                break;
            }
        }
        file.close();

        if(readError) return ""; // Return empty on error

        // Return the next ID
        return prefix + to_string(maxId + 1);
    }
    // --- END NEW HELPER ---

public:
    // Constructor updated (SaleID will be set during addSalesEntry)
    SalesEntry(string ic, int qty, string date) :
        itemCode(ic), quantity(qty), salesDate(date), saleID("") {} // Initialize saleID as empty

    // Overload constructor potentially needed if loading existing entries
    SalesEntry(string sid, string ic, int qty, string date) :
        saleID(sid), itemCode(ic), quantity(qty), salesDate(date) {}

    // Add sales entry - Updated to generate and save SaleID
    bool addSalesEntry() {
        // Basic validations remain the same...
        if (itemCode.empty() || quantity <= 0 || salesDate.empty()) { /* ... */ return false; }
        if (!doesItemExist(itemCode)) { /* ... */ return false; }
        if (!isDateValid(salesDate)) { /* ... */ return false; }

        // *** NEW: Generate unique Sale ID ***
        this->saleID = generateNewSaleID();
        if (this->saleID.empty()) {
             cerr << "Error: Could not generate a unique Sale ID. Sale not recorded." << endl;
             return false; // Cannot proceed without a sale ID
        }
        // *** END NEW ***

        // --- Decrease stock BEFORE recording sale (remains the same) ---
        if (!Item::decreaseStock(itemCode, quantity)) { /* ... */ return false; }

        // Append sale to sales.txt - *** Updated format ***
        ofstream file(SALES_FILE, ios::app);
        if (!file.is_open()) { /* ... Error handling ... */ return false; }
        // New format: SaleID ItemCode Quantity SalesDate
        file << this->saleID << " " << itemCode << " " << quantity << " " << salesDate << endl;
        if (file.fail()) { /* ... Error handling ... */ file.close(); return false; }
        file.close();
        cout << "Sales entry '" << this->saleID << "' added and stock updated successfully." << endl;
        return true;
    }

     // List sales entries - Updated to display SaleID
     static void listSalesEntries() {
        cout << "\n--- Daily Sales Entries ---" << endl;
        cout << "SaleID  Item Code  Quantity   Date" << endl; // Updated header
        cout << "--------------------------------------" << endl;
        ifstream file(SALES_FILE);
        if (!file.is_open()) { /* ... Error handling ... */ return; }
        string line;
        int count = 0;
        while (getline(file, line)) {
            if (line.empty()) continue;
            string id, code, date; // Added id variable
            int qty;
            istringstream iss(line);
            // Updated parsing for new format
            if (iss >> id >> code >> qty >> date) {
                printf("%-7s %-10s %-10d %-10s\n", id.c_str(), code.c_str(), qty, date.c_str()); // Updated format
                count++;
            } else {
                cerr << "Warning: Skipping malformed line in sales file: " << line << endl;
            }
            if (file.fail() && !file.eof()) { /* ... Error handling ... */ break; }
        }
        if (count == 0) { cout << "(No sales entries found)" << endl; }
        cout << "--------------------------------------" << endl;
        file.close();
    }

    // Edit sales entry - uses safe write. Key is ItemCode + SalesDate perhaps?
    // For simplicity, let's make ItemCode the key FOR EDITING, assuming one entry per item per day isn't enforced strictly by edit.
    // A better key would be a unique SaleID, or composite ItemCode+Date.
    // This simplified edit finds the *first* match for ItemCode and edits it.
    // Edit sales entry - Updated to use SaleID as the key
    // *** NOTE: Only Quantity and Date can be edited. SaleID, ItemCode are fixed. ***
    static bool editSaleEntry(const string& saleIDToEdit) { // Parameter changed to SaleID
        vector<string> fileLines;
        ifstream infile(SALES_FILE);
        if (!infile.is_open()) { /* ... Error handling ... */ return false; }
        string line;
        bool found = false;
        bool readError = false;
        int lineIndex = -1;
        int currentLineNum = 0;
        string currentItemCode = ""; // Store item code from the found line

        while (getline(infile, line)) {
            fileLines.push_back(line);
            if (!found) {
                istringstream iss(line);
                string currentSaleID;
                // *** FIX: Find line based on SaleID (first field) ***
                if (iss >> currentSaleID) {
                    if (currentSaleID == saleIDToEdit) {
                        found = true;
                        lineIndex = currentLineNum;
                        // Try to read the item code from this line as well
                        string tempQtyStr, tempDate;
                        if (!(iss >> currentItemCode >> tempQtyStr >> tempDate)) {
                             cerr << "Warning: Found SaleID but could not parse full line during edit: " << line << endl;
                             // Decide how to handle - maybe abort?
                             readError = true; // Treat as error
                        }
                    }
                } else if (!line.empty()) {
                     cerr << "Warning: Could not parse Sale ID from line during edit: " << line << endl;
                }
            }
            currentLineNum++;
            if (infile.fail() && !infile.eof()) { /* ... Error handling ... */ readError = true; break; }
        }
        infile.close();

        if (readError) { cerr << "Error reading sales file during edit." << endl; return false; }
        if (!found) { cout << "Sales entry with ID '" << saleIDToEdit << "' not found for editing." << endl; return false; }

        // Get new data (Quantity, Date ONLY)
        cout << "Editing Sales Entry ID: " << saleIDToEdit << " (Item Code: " << currentItemCode << ")" << endl;
        int newQuantity = getValidatedIntegerInput("Enter new quantity: ");
        if (newQuantity <= 0) { cerr << "Error: Quantity must be positive." << endl; return false;}

        string newSalesDate;
        while (true) {
            cout << "Enter new sales date (YYYY-MM-DD): ";
            getline(cin, newSalesDate);
            if (isDateValid(newSalesDate)) break;
        }

        // Update line in vector - keeping original SaleID and ItemCode
        fileLines[lineIndex] = saleIDToEdit + " " + currentItemCode + " " + to_string(newQuantity) + " " + newSalesDate;

        // --- Safe Write ---
        if (!writeVectorToFile(fileLines, SALES_FILE)) { // Use the static helper from SalesEntry (or FileManager)
             cerr << "Error writing updated sales data to file." << endl;
             return false;
        }

        cout << "Sales entry '" << saleIDToEdit << "' updated successfully." << endl;
        return true;
    }

    // Delete sales entry - simplified: deletes *first* entry matching item code.
    // A better approach needs unique Sale IDs.
     // Delete sales entry - Updated to use SaleID
     static bool deleteSaleEntry(const string& saleIDToDelete) { // Parameter changed to SaleID
        cout << "Attempting to delete sales entry with ID '" << saleIDToDelete << "'." << endl;
        FileManager fm;
        // Use base class delete which works on the first field (now SaleID)
        return fm.deleteEntryById(saleIDToDelete, SALES_FILE);
    }

private:
     static bool doesItemExist(const string& itemCode) {
        FileManager fm;
        return fm.doesIdExist(itemCode, ITEMS_FILE);
     }
     // Helper for safe write, used by Edit
     static bool writeVectorToFile(const vector<string>& lines, const string& filename) {
        string tempFilename = filename + ".tmp";
        ofstream outfile(tempFilename);
        if (!outfile.is_open()) { cerr << "Error: Could not create temporary file: " << tempFilename << endl; return false; }
        bool writeError = false;
        for (const string& entry : lines) {
            outfile << entry << endl;
            if (outfile.fail()) { cerr << "Error: Failed writing to temporary file: " << tempFilename << endl; writeError = true; break; }
        }
        outfile.close();
        if (writeError) { remove(tempFilename.c_str()); return false; }
        if (remove(filename.c_str()) != 0) { perror(("Error deleting original file " + filename).c_str()); remove(tempFilename.c_str()); return false; }
        if (rename(tempFilename.c_str(), filename.c_str()) != 0) { perror(("Error renaming temporary file to " + filename).c_str()); return false; }
        return true;
     }

};


// --- PurchaseRequisition Class ---
class PurchaseRequisition : protected FileManager {
friend class Application;
private:
    string PRID;
    string itemCode;
    int quantity;
    string requiredDate;
    string supplierCode;
    string salesManager; // User ID of SM

public:
    PurchaseRequisition(string prid, string ic, int qty, string date, string sc, string sm) :
     PRID(prid), itemCode(ic), quantity(qty), requiredDate(date), supplierCode(sc), salesManager(sm) {}

    // Create PR - Validates and appends
    bool createPR() {
        if (PRID.empty() || itemCode.empty() || quantity <= 0 || requiredDate.empty() || supplierCode.empty() || salesManager.empty()) {
            cerr << "Error: All fields are required for Purchase Requisition." << endl; return false;
        }
        if (doesIdExist(PRID, REQUISITIONS_FILE)) {
             cerr << "Error: PRID '" << PRID << "' already exists." << endl; return false;
        }
        if (!doesItemExist(itemCode)) {
             cerr << "Error: Item Code '" << itemCode << "' does not exist." << endl; return false;
        }
        if (!isDateValid(requiredDate)) { return false; } // Error printed inside
        if (!doesSupplierExist(supplierCode)) {
             cerr << "Error: Supplier Code '" << supplierCode << "' does not exist." << endl; return false;
        }
         if (!doesSalesManagerExist(salesManager)) {
              cerr << "Error: Sales Manager ID '" << salesManager << "' does not exist or is not an SM." << endl; return false;
         }

        // Append to file
        ofstream file(REQUISITIONS_FILE, ios::app);
        if (!file.is_open()) { /* ... Error handling ... */ return false; }
        file << PRID << " " << itemCode << " " << quantity << " " << requiredDate << " " << supplierCode << " " << salesManager << endl;
        if (file.fail()) { /* ... Error handling ... */ file.close(); return false; }
        file.close();
        cout << "Purchase Requisition '" << PRID << "' created successfully." << endl;
        return true;
    }

    // Display all PRs
    static void displayPRs() {
        cout << "\n--- Purchase Requisitions ---" << endl;
        // Adjust formatting based on expected field lengths
        cout << "PRID    ItemCode Qty   ReqDate    Supplier SalesMgr" << endl;
        cout << "-----------------------------------------------------" << endl;
        ifstream file(REQUISITIONS_FILE);
        if (!file.is_open()) { /* ... Error handling ... */ return; }
        string line;
        int count = 0;
        while (getline(file, line)) {
             if (line.empty()) continue;
             // Simple parsing for now, assumes no spaces in fields except maybe date (but format is fixed)
             string prid, ic, date, sc, sm;
             int qty;
             istringstream iss(line);
             if (iss >> prid >> ic >> qty >> date >> sc >> sm) {
                 printf("%-7s %-8s %-5d %-10s %-8s %-8s\n", prid.c_str(), ic.c_str(), qty, date.c_str(), sc.c_str(), sm.c_str());
                 count++;
             } else {
                 cerr << "Warning: Skipping malformed line in requisitions file: " << line << endl;
             }
             if (file.fail() && !file.eof()) { /* ... Error handling ... */ break; }
        }
         if (count == 0) { cout << "(No purchase requisitions found)" << endl; }
        cout << "-----------------------------------------------------" << endl;
        file.close();
    }


    static bool editPR(const string& PRIDToEdit) {
        vector<string> fileLines;
        ifstream infile(REQUISITIONS_FILE);
        if (!infile.is_open()) { /* ... Error handling ... */ return false; }
        string line;
        bool found = false;
        bool readError = false;
        int lineIndex = -1;
        int currentLineNum = 0; // Keep track of line number

        while (getline(infile, line)) {
            fileLines.push_back(line);
            if (!found) {
                // *** FIX: Added logic to find the line ***
                istringstream iss(line);
                string currentPRID;
                if (iss >> currentPRID) { // Parse the first word (ID)
                    if (currentPRID == PRIDToEdit) {
                        found = true;
                        lineIndex = currentLineNum; // Store index
                    }
                } else if (!line.empty()){
                     cerr << "Warning: Could not parse PR ID from line during edit: " << line << endl;
                }
            }
             currentLineNum++; // Increment line number
             if (infile.fail() && !infile.eof()) { /* ... Error handling ... */ readError = true; break; }
        }
        infile.close();

        if (readError) { cerr << "Error reading requisitions file during edit." << endl; return false;}
        if (!found) { cout << "Purchase Requisition with ID '" << PRIDToEdit << "' not found for editing." << endl; return false; }

        // ... (rest of the function: get new data, update fileLines[lineIndex], writeVectorToFile) ...
        // NOTE: The bug fix for the filename is in the next section!

        // Get new data for non-key fields
        cout << "Editing Purchase Requisition ID: " << PRIDToEdit << endl;
        string newItemCode, newRequiredDate, newSupplierCode, newSalesManager;
        int newQuantity;

        cout << "Enter new item code: "; getline(cin, newItemCode);
        if (!doesItemExist(newItemCode)) { cerr << "Error: Item code '" << newItemCode << "' does not exist." << endl; return false; }

        // *** FIX: Fetch supplier automatically based on NEW item code ***
        newSupplierCode = Item::getSupplierForItem(newItemCode);
         if (newSupplierCode.empty()) {
            cerr << "Error: Could not find supplier for new item code '" << newItemCode << "'. Cannot edit PR." << endl;
            return false;
         } else {
            cout << "--> Found Supplier for new item '" << newItemCode << "': " << newSupplierCode << endl;
         }
         // *** END FIX ***

        newQuantity = getValidatedIntegerInput("Enter new quantity: ");
        if (newQuantity <= 0) { cerr << "Error: Quantity must be positive." << endl; return false;}

        while (true) {
            cout << "Enter new required date (YYYY-MM-DD): "; getline(cin, newRequiredDate);
            if (isDateValid(newRequiredDate)) break;
        }

        // REMOVED: No longer need to ask for supplier code
        // cout << "Enter new supplier code: "; getline(cin, newSupplierCode);
        // if (!doesSupplierExist(newSupplierCode)) { /* ... Error handling ... */ return false; }

        cout << "Enter new sales manager ID: "; getline(cin, newSalesManager);
        if (!doesSalesManagerExist(newSalesManager)) { cerr << "Error: Sales Manager ID '" << newSalesManager << "' does not exist or is not an SM." << endl; return false; }


        // Update line in vector
        fileLines[lineIndex] = PRIDToEdit + " " + newItemCode + " " + to_string(newQuantity) + " " + newRequiredDate + " " + newSupplierCode + " " + newSalesManager;

        // --- Safe Write --- (Filename corrected in next fix)
        if (!FileManager::writeVectorToFile(fileLines, REQUISITIONS_FILE)) { // Corrected Filename Here Too!
            cerr << "Error writing updated PR data to file." << endl;
            return false;
        }

        cout << "Purchase Requisition '" << PRIDToEdit << "' updated successfully." << endl;
        return true;
    }


    // Delete PR
    static bool deletePR(const string& PRIDToDelete) {
        FileManager fm;
        return fm.deleteEntryById(PRIDToDelete, REQUISITIONS_FILE);
    }

private:
    // Validation helpers
     static bool doesItemExist(const string& itemCode) { /* ... check ITEMS_FILE ... */ FileManager fm; return fm.doesIdExist(itemCode, ITEMS_FILE); }
     static bool doesSupplierExist(const string& supplierCode) { /* ... check SUPPLIERS_FILE ... */ FileManager fm; return fm.doesIdExist(supplierCode, SUPPLIERS_FILE); }
     static bool doesSalesManagerExist(const string& userId) {
         // Check if user ID exists AND is an SM
         ifstream file(USERS_FILE);
         if (!file.is_open()) { return false; }
         string line;
         bool found = false;
         while (getline(file, line)) {
             string savedUser, savedPass, savedLevel;
             istringstream iss(line);
             if (iss >> savedUser >> savedPass >> savedLevel) {
                 if (savedUser == userId && savedLevel == "SM") {
                     found = true;
                     break;
                 }
             }
         }
         file.close();
         return found;
     }
};


// --- PurchaseOrder Class ---
class PurchaseOrder : protected FileManager {
friend class Application;
private:
    string POID;
    string PRID; // Should exist in purchase_requisitions.txt
    string purchaseManager; // User ID of PM

public:
    PurchaseOrder(string poid, string prid, string pm) :
        POID(poid), PRID(prid), purchaseManager(pm) {}

    // Generate PO - Validates and appends
    bool generatePO() {
         if (POID.empty() || PRID.empty() || purchaseManager.empty()) {
             cerr << "Error: POID, PRID, and Purchase Manager ID are required." << endl; return false;
         }
         if (doesIdExist(POID, ORDERS_FILE)) { // Check if POID is unique
             cerr << "Error: POID '" << POID << "' already exists." << endl; return false;
         }
         if (!doesPRExist(PRID)) { // Check if PRID exists in REQUISITIONS file
              cerr << "Error: Purchase Requisition ID '" << PRID << "' does not exist." << endl; return false;
         }
         if (!doesPurchaseManagerExist(purchaseManager)) {
              cerr << "Error: Purchase Manager ID '" << purchaseManager << "' does not exist or is not a PM." << endl; return false;
         }

        // Append to file
        ofstream file(ORDERS_FILE, ios::app);
        if (!file.is_open()) { /* ... Error handling ... */ return false; }
        file << POID << " " << PRID << " " << purchaseManager << endl;
        if (file.fail()) { /* ... Error handling ... */ file.close(); return false; }
        file.close();
        cout << "Purchase Order '" << POID << "' generated successfully." << endl;
        return true;
    }

    // List POs
    static void listPOs() {
        cout << "\n--- Purchase Orders ---" << endl;
        cout << "POID    PRID    PurchaseMgr" << endl;
        cout << "-----------------------------" << endl;
        ifstream file(ORDERS_FILE);
        if (!file.is_open()) { /* ... Error handling ... */ return; }
        string line;
        int count = 0;
        while (getline(file, line)) {
            if (line.empty()) continue;
            string poid, prid, pm;
            istringstream iss(line);
            if (iss >> poid >> prid >> pm) {
                 printf("%-7s %-7s %-11s\n", poid.c_str(), prid.c_str(), pm.c_str());
                 count++;
            } else {
                 cerr << "Warning: Skipping malformed line in orders file: " << line << endl;
            }
            if (file.fail() && !file.eof()) { /* ... Error handling ... */ break; }
        }
        if (count == 0) { cout << "(No purchase orders found)" << endl; }
        cout << "-----------------------------" << endl;
        file.close();
    }

    // In PurchaseOrder class:
    static bool editPO(const string& POIDToEdit) {
        vector<string> fileLines;
        ifstream infile(ORDERS_FILE);
        if (!infile.is_open()) { /* ... Error handling ... */ return false; }
        string line;
        bool found = false;
        bool readError = false;
        int lineIndex = -1;
        int currentLineNum = 0; // Keep track of line number

        while (getline(infile, line)) {
            fileLines.push_back(line);
            if (!found) {
                // *** FIX: Added logic to find the line ***
                 istringstream iss(line);
                 string currentPOID;
                 if (iss >> currentPOID) { // Parse the first word (ID)
                     if (currentPOID == POIDToEdit) {
                        found = true;
                        lineIndex = currentLineNum; // Store index
                    }
                } else if (!line.empty()){
                     cerr << "Warning: Could not parse PO ID from line during edit: " << line << endl;
                }
            }
            currentLineNum++; // Increment line number
            if (infile.fail() && !infile.eof()) { /* ... Error handling ... */ readError = true; break; }
        }
        infile.close();

        if (readError) { cerr << "Error reading orders file during edit." << endl; return false;}
        if (!found) { cout << "Purchase Order with ID '" << POIDToEdit << "' not found for editing." << endl; return false; }

        // Get new data (PRID, PurchaseManager)
        cout << "Editing Purchase Order ID: " << POIDToEdit << endl;
        string newPRID, newPurchaseManager;

        cout << "Enter new PRID: "; getline(cin, newPRID);
        if (!doesPRExist(newPRID)) { cerr << "Error: PRID '" << newPRID << "' does not exist." << endl; return false; }

        cout << "Enter new purchase manager ID: "; getline(cin, newPurchaseManager);
        if (!doesPurchaseManagerExist(newPurchaseManager)) { cerr << "Error: Purchase Manager ID '" << newPurchaseManager << "' does not exist or is not a PM." << endl; return false; }

        // Update line in vector
        fileLines[lineIndex] = POIDToEdit + " " + newPRID + " " + newPurchaseManager;

        // --- Safe Write ---
        if (!FileManager::writeVectorToFile(fileLines, ORDERS_FILE)) {
            cerr << "Error writing updated PO data to file." << endl;
            return false;
        }

        cout << "Purchase Order '" << POIDToEdit << "' updated successfully." << endl;
        return true;
    }

    // Delete PO
    static bool deletePO(const string& POIDToDelete) {
        FileManager fm;
        return fm.deleteEntryById(POIDToDelete, ORDERS_FILE);
    }

private:
    // Validation helpers
     static bool doesPRExist(const string& prid) {
         FileManager fm;
         return fm.doesIdExist(prid, REQUISITIONS_FILE); // Check the correct file
     }
     static bool doesPurchaseManagerExist(const string& userId) {
        // Check if user ID exists AND is a PM
         ifstream file(USERS_FILE);
         if (!file.is_open()) { return false; }
         string line;
         bool found = false;
         while (getline(file, line)) {
             string savedUser, savedPass, savedLevel;
             istringstream iss(line);
             if (iss >> savedUser >> savedPass >> savedLevel) {
                 if (savedUser == userId && savedLevel == "PM") {
                     found = true;
                     break;
                 }
             }
         }
         file.close();
         return found;
     }
};


// --- Application Class (UI and Control Flow) ---
class Application {
public:
    // Display menus based on access level
    static void displayMenu(const string& accessLevel) {
        cout << "\n==================== Main Menu (" << accessLevel << ") ===================" << endl;
        if (accessLevel == "Admin") {
            cout << " 1. Item Management (Add/Edit/Delete/List)" << endl;
            cout << " 2. Supplier Management (Add/Edit/Delete/List)" << endl;
            cout << " 3. Sales Entry Management (Add/Edit/Delete/List)" << endl;
            cout << " 4. Purchase Requisition Management (Add/Edit/Delete/List)" << endl;
            cout << " 5. Purchase Order Management (Add/Edit/Delete/List)" << endl;
            cout << " 6. User Registration" << endl; // Combined Add/List implicitly part of management menus
        } else if (accessLevel == "SM") {
            cout << " 1. Item Management (Add/Edit/Delete/List)" << endl;
            cout << " 2. Supplier Management (Add/Edit/Delete/List)" << endl;
            cout << " 3. Sales Entry Management (Add/Edit/Delete/List)" << endl;
            cout << " 4. Purchase Requisition Management (Add/Edit/Delete/List)" << endl;
            cout << " 5. View Purchase Orders" << endl;
        } else if (accessLevel == "PM") {
            cout << " 1. View Items" << endl;
            cout << " 2. View Suppliers" << endl;
            cout << " 3. View Purchase Requisitions" << endl;
            cout << " 4. Purchase Order Management (Add/Edit/Delete/List)" << endl;
        }
        cout << " 0. Logout" << endl;
        cout << "===========================================================\n" << endl;
    }

    // Main loop after user logs in
    static void handleUserInput(const string& accessLevel) {
        string choice;
        while (true) {
            displayMenu(accessLevel);
            cout << "Enter your choice: ";
            getline(cin, choice); // Read full line for choice

            if (choice == "0") {
                cout << "Logging out..." << endl;
                return; // Return to main loop (user access selection)
            }

            // --- Admin Menu Choices ---
            if (accessLevel == "Admin") {
                if (choice == "1") handleItemMenu();
                else if (choice == "2") handleSupplierMenu();
                else if (choice == "3") handleSaleEntryMenu();
                else if (choice == "4") handlePRMenu();
                else if (choice == "5") handlePOMenu();
                else if (choice == "6") registerNewUser();
                else cout << "Invalid choice. Please try again." << endl;
            }
            // --- Sales Manager (SM) Menu Choices ---
            else if (accessLevel == "SM") {
                if (choice == "1") handleItemMenu();
                else if (choice == "2") handleSupplierMenu();
                else if (choice == "3") handleSaleEntryMenu();
                else if (choice == "4") handlePRMenu();
                else if (choice == "5") PurchaseOrder::listPOs(); // View only
                else cout << "Invalid choice. Please try again." << endl;
            }
            // --- Purchase Manager (PM) Menu Choices ---
            else if (accessLevel == "PM") {
                if (choice == "1") Item::listItems();            // View only
                else if (choice == "2") Supplier::listSuppliers();    // View only
                else if (choice == "3") PurchaseRequisition::displayPRs(); // View only
                else if (choice == "4") handlePOMenu();
                else cout << "Invalid choice. Please try again." << endl;
            }
             cout << "\nPress Enter to continue..." << endl;
             cin.ignore(numeric_limits<streamsize>::max(), '\n'); // Pause screen
        }
    }

private: // --- Sub-Menu Handlers ---

    static void registerNewUser() {
        cout << "\n--- New User Registration ---" << endl;
        string password, accessLevel;
        cout << "Enter access level (SM/PM/Admin): ";
        getline(cin, accessLevel);
        // Basic validation for input format
        if (accessLevel != "SM" && accessLevel != "PM" && accessLevel != "Admin") {
             cerr << "Error: Invalid access level entered. Please use SM, PM, or Admin." << endl;
             return;
        }
        cout << "Enter password for new user: ";
        getline(cin, password);
        if (password.empty()) {
             cerr << "Error: Password cannot be empty." << endl;
             return;
        }

        User newUser(password, accessLevel); // Pass only needed info
        newUser.registerUser(); // Method handles validation and saving
    }

    // --- Item Management ---
    static void handleItemMenu() {
        string choice;
        while (true) {
            cout << "\n\t--- Item Management Menu ---" << endl;
            cout << "\t1. Add New Item" << endl;
            cout << "\t2. Edit Existing Item" << endl;
            cout << "\t3. Delete Item" << endl;
            cout << "\t4. List All Items" << endl;
            cout << "\t0. Back to Main Menu" << endl;
            cout << "\t----------------------------" << endl;
            cout << "Enter choice: ";
            getline(cin, choice);

            if (choice == "0") return;
            else if (choice == "1") addItem();
            else if (choice == "2") {
                string code;
                cout << "Enter Item Code to Edit: "; getline(cin, code);
                Item::editItem(code);
            }
            else if (choice == "3") {
                string code;
                cout << "Enter Item Code to Delete: "; getline(cin, code);
                Item::deleteItem(code);
            }
             else if (choice == "4") Item::listItems();
            else cout << "Invalid choice." << endl;
             cout << "\nPress Enter to continue..." << endl;
             cin.ignore(numeric_limits<streamsize>::max(), '\n');
        }
    }
    static void addItem() {
        cout << "\n--- Add New Item ---" << endl;
        string code, name, supId;
        int initialQty; // Add variable for quantity
        cout << "Enter Item Code: "; getline(cin, code);
        cout << "Enter Item Name: "; getline(cin, name);
        cout << "Enter Supplier ID: "; getline(cin, supId);
        initialQty = getValidatedIntegerInput("Enter Initial Quantity In Stock: "); // Get initial stock
        Item newItem(code, name, supId, initialQty); // Pass quantity to constructor
        newItem.addItem();
    }

    // --- Supplier Management ---
     static void handleSupplierMenu() {
         string choice;
         while(true) {
             cout << "\n\t--- Supplier Management Menu ---" << endl;
             cout << "\t1. Add New Supplier" << endl;
             cout << "\t2. Edit Existing Supplier" << endl;
             cout << "\t3. Delete Supplier" << endl;
             cout << "\t4. List All Suppliers" << endl;
             cout << "\t0. Back to Main Menu" << endl;
             cout << "\t--------------------------------" << endl;
             cout << "Enter choice: ";
             getline(cin, choice);

             if (choice == "0") return;
             else if (choice == "1") addSupplier();
             else if (choice == "2") { string code; cout << "Enter Supplier Code to Edit: "; getline(cin, code); Supplier::editSupplier(code); }
             else if (choice == "3") { string code; cout << "Enter Supplier Code to Delete: "; getline(cin, code); Supplier::deleteSupplier(code); }
             else if (choice == "4") Supplier::listSuppliers();
             else cout << "Invalid choice." << endl;
              cout << "\nPress Enter to continue..." << endl;
              cin.ignore(numeric_limits<streamsize>::max(), '\n');
         }
     }
     static void addSupplier() {
        cout << "\n--- Add New Supplier ---" << endl;
        string code, name; // Removed itemId variable
        cout << "Enter Supplier Code: "; getline(cin, code);
        cout << "Enter Supplier Name: "; getline(cin, name);
        // REMOVED: Prompt for item ID
        Supplier newSupplier(code, name); // Updated constructor call
        newSupplier.addSupplier();
     }

      // --- Sales Entry Management ---
    static void handleSaleEntryMenu() {
        string choice;
        while(true) {
            cout << "\n\t--- Sales Entry Management Menu ---" << endl;
            cout << "\t1. Add New Sales Entry" << endl;
            // *** Updated Menu Text ***
            cout << "\t2. Edit Sales Entry (by Sale ID)" << endl;
            cout << "\t3. Delete Sales Entry (by Sale ID)" << endl;
            // *** End Update ***
            cout << "\t4. List All Sales Entries" << endl;
            cout << "\t0. Back to Main Menu" << endl;
            cout << "\t-------------------------------------" << endl;
            cout << "Enter choice: ";
            getline(cin, choice);

            if (choice == "0") return;
            else if (choice == "1") addSalesEntry();
            // *** Updated calls to prompt for SaleID ***
            else if (choice == "2") {
                 string id;
                 cout << "Enter Sale ID of Sale to Edit: "; // Ask for SaleID
                 getline(cin, id);
                 SalesEntry::editSaleEntry(id); // Pass SaleID
             }
            else if (choice == "3") {
                 string id;
                 cout << "Enter Sale ID of Sale to Delete: "; // Ask for SaleID
                 getline(cin, id);
                 SalesEntry::deleteSaleEntry(id); // Pass SaleID
            }
            // *** End Update ***
            else if (choice == "4") SalesEntry::listSalesEntries();
            else cout << "Invalid choice." << endl;
             cout << "\nPress Enter to continue..." << endl;
             cin.ignore(numeric_limits<streamsize>::max(), '\n');
        }
    }
      // addSalesEntry remains the same as it now handles ID generation internally
    static void addSalesEntry() {
        cout << "\n--- Add New Sales Entry ---" << endl;
        string itemCode, salesDate;
        cout << "Enter Item Code: "; getline(cin, itemCode);
        int quantity = getValidatedIntegerInput("Enter Quantity: "); // Use safe input helper
        while(true) {
            cout << "Enter Sales Date (YYYY-MM-DD): "; getline(cin, salesDate);
            if (isDateValid(salesDate)) break; // Use improved validation
        }
        SalesEntry newEntry(itemCode, quantity, salesDate); // SaleID is generated inside addSalesEntry
        newEntry.addSalesEntry();
    }

    // --- Purchase Requisition (PR) Management ---
     static void handlePRMenu() {
          string choice;
          while(true) {
              cout << "\n\t--- Purchase Requisition Menu ---" << endl;
              cout << "\t1. Create New Purchase Requisition" << endl;
              cout << "\t2. Edit Existing Purchase Requisition" << endl;
              cout << "\t3. Delete Purchase Requisition" << endl;
              cout << "\t4. List All Purchase Requisitions" << endl;
              cout << "\t0. Back to Main Menu" << endl;
              cout << "\t-----------------------------------" << endl;
              cout << "Enter choice: ";
              getline(cin, choice);

              if (choice == "0") return;
              else if (choice == "1") createPR();
              else if (choice == "2") { string id; cout << "Enter PR ID to Edit: "; getline(cin, id); PurchaseRequisition::editPR(id); }
              else if (choice == "3") { string id; cout << "Enter PR ID to Delete: "; getline(cin, id); PurchaseRequisition::deletePR(id); }
              else if (choice == "4") PurchaseRequisition::displayPRs();
              else cout << "Invalid choice." << endl;
               cout << "\nPress Enter to continue..." << endl;
               cin.ignore(numeric_limits<streamsize>::max(), '\n');
          }
     }
     static void createPR() {
        cout << "\n--- Create New Purchase Requisition ---" << endl;
        string prid, ic, date, /* REMOVED sc */ sm; // Remove supplier code variable
        int qty;
        cout << "Enter PR ID: "; getline(cin, prid);
        cout << "Enter Item Code: "; getline(cin, ic);

        // --- NEW: Fetch supplier ID ---
        string fetchedSupplierCode = Item::getSupplierForItem(ic);
        if (fetchedSupplierCode.empty()) {
            cerr << "Error: Could not find item '" << ic << "' or its supplier in " << ITEMS_FILE << ". Cannot create PR." << endl;
            return;
        } else {
            cout << "--> Found Supplier for item '" << ic << "': " << fetchedSupplierCode << endl;
        }
        // --- END NEW ---

        qty = getValidatedIntegerInput("Enter Quantity: ");
        while(true) {
            cout << "Enter Required Date (YYYY-MM-DD): "; getline(cin, date);
            if (isDateValid(date)) break;
        }
        // REMOVED: cout << "Enter Supplier Code: "; getline(cin, sc);
        cout << "Enter Sales Manager ID: "; getline(cin, sm);

        // Use fetchedSupplierCode instead of sc
        PurchaseRequisition newPR(prid, ic, qty, date, fetchedSupplierCode, sm);
        newPR.createPR(); // createPR method itself needs to validate the fetchedSupplierCode still
     }

     // --- Purchase Order (PO) Management ---
      static void handlePOMenu() {
           string choice;
           while(true) {
               cout << "\n\t--- Purchase Order Menu ---" << endl;
               cout << "\t1. Generate New Purchase Order" << endl;
               cout << "\t2. Edit Existing Purchase Order" << endl;
               cout << "\t3. Delete Purchase Order" << endl;
               cout << "\t4. List All Purchase Orders" << endl;
               cout << "\t0. Back to Main Menu" << endl;
               cout << "\t-----------------------------" << endl;
               cout << "Enter choice: ";
               getline(cin, choice);

               if (choice == "0") return;
               else if (choice == "1") generatePO();
               else if (choice == "2") { string id; cout << "Enter PO ID to Edit: "; getline(cin, id); PurchaseOrder::editPO(id); }
               else if (choice == "3") { string id; cout << "Enter PO ID to Delete: "; getline(cin, id); PurchaseOrder::deletePO(id); }
               else if (choice == "4") PurchaseOrder::listPOs();
               else cout << "Invalid choice." << endl;
                cout << "\nPress Enter to continue..." << endl;
                cin.ignore(numeric_limits<streamsize>::max(), '\n');
           }
      }
      static void generatePO() {
          cout << "\n--- Generate New Purchase Order ---" << endl;
          string poid, prid, pm;
          cout << "Enter PO ID: "; getline(cin, poid);
          cout << "Enter corresponding PR ID: "; getline(cin, prid);
          cout << "Enter Purchase Manager ID: "; getline(cin, pm);
          PurchaseOrder newPO(poid, prid, pm);
          newPO.generatePO();
      }
};


// --- Main Function ---
int main() {
    string username, password, accessLevel, choice;

    while (true) {
        cout << "\n======= User Access =======" << endl;
        cout << "1. Admin Login" << endl;
        cout << "2. Sales Manager (SM) Login" << endl;
        cout << "3. Purchase Manager (PM) Login" << endl;
        cout << "0. Exit System" << endl;
        cout << "===========================" << endl;
        cout << "Enter your choice: ";
        getline(cin, choice); // Read choice robustly

        string selectedRole = "";
        if (choice == "0") {
            cout << "Exiting the system..." << endl;
            return 0;
        } else if (choice == "1") {
            selectedRole = "Admin";
        } else if (choice == "2") {
            selectedRole = "SM";
        } else if (choice == "3") {
            selectedRole = "PM";
        } else {
            cerr << "Invalid choice. Please try again." << endl;
            cout << "\nPress Enter to continue..." << endl;
            cin.ignore(numeric_limits<streamsize>::max(), '\n'); // Pause screen
            continue; // Loop back to menu
        }

        cout << "\n==== " << selectedRole << " Login ====" << endl;
        cout << "Username: ";
        getline(cin, username);
        cout << "Password: ";
        getline(cin, password);

        string grantedLevel; // Will be filled by login function on success
        if (User::login(username, password, grantedLevel)) {
             // Optional: Double-check if the grantedLevel matches the selectedRole expected
             if (grantedLevel != selectedRole) {
                  cerr << "Warning: Login successful, but access level (" << grantedLevel
                       << ") does not match selected role (" << selectedRole << "). Proceeding with granted level." << endl;
             }
            cout << "Login successful." << endl;
            cout << "\nPress Enter to proceed to the main menu..." << endl;
            cin.ignore(numeric_limits<streamsize>::max(), '\n'); // Pause screen
            Application::handleUserInput(grantedLevel); // Use the level confirmed by login
        } else {
            cerr << "Login Failed: Invalid username or password for selected role." << endl;
            cout << "\nPress Enter to continue..." << endl;
            cin.ignore(numeric_limits<streamsize>::max(), '\n'); // Pause screen
        }
    } // End main loop

    return 0; // Should not be reached due to loop/exit logic
}