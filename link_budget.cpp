#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <ctime>
#include <cmath>
#include <regex>
#include <uuid/uuid.h>
#include <sys/stat.h>
#include <unistd.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace std;

const string SESSION_DIR = "/tmp/sessions/";
const int SESSION_EXPIRY = 3600; // 1 hour

// Function to generate a unique session ID
string generate_session_id() {
    uuid_t uuid;
    char uuid_str[37];
    uuid_generate(uuid);
    uuid_unparse_lower(uuid, uuid_str);
    return string(uuid_str);
}

// Function to check if a file exists
bool file_exists(const string& name) {
    struct stat buffer;   
    return (stat(name.c_str(), &buffer) == 0); 
}

// Session class for managing user sessions
class Session {
private:
    string id;
    json data;
    time_t last_accessed;

    string get_file_path() {
        return SESSION_DIR + id + ".json";
    }

public:
    Session(string session_id = "") {
        if (session_id.empty()) {
            id = generate_session_id();
            last_accessed = time(nullptr);
        } else {
            id = session_id;
            load();
        }
    }

    void set(const string& key, const string& value) {
        data[key] = value;
        last_accessed = time(nullptr);
        save();
    }

    string get(const string& key, const string& default_value = "") {
        if (data.contains(key)) {
            last_accessed = time(nullptr);
            save();
            return data[key];
        }
        return default_value;
    }

    void save() {
        ofstream file(get_file_path());
        json session_data;
        session_data["data"] = data;
        session_data["last_accessed"] = last_accessed;
        file << session_data.dump();
        file.close();
    }

    void load() {
        string file_path = get_file_path();
        if (file_exists(file_path)) {
            ifstream file(file_path);
            json session_data = json::parse(file);
            data = session_data["data"];
            last_accessed = session_data["last_accessed"];
            file.close();

            if (time(nullptr) - last_accessed > SESSION_EXPIRY) {
                // Session expired, clear data
                data.clear();
            }
        }
    }

    string get_id() {
        return id;
    }

    bool is_expired() {
        return time(nullptr) - last_accessed > SESSION_EXPIRY;
    }
};

// Function to parse CGI input
map<string, string> parse_cgi_input() {
    map<string, string> data;
    string input;
    getline(cin, input);

    regex pattern("([^&=]+)=([^&]*)");
    smatch match;
    string::const_iterator start = input.begin();
    string::const_iterator end = input.end();

    while (regex_search(start, end, match, pattern)) {
        data[match[1]] = match[2];
        start = match.suffix().first;
    }

    return data;
}

// Function to escape HTML special characters (XSS prevention)
string html_escape(const string& data) {
    string buffer;
    for (char c : data) {
        switch (c) {
            case '&':  buffer.append("&amp;");  break;
            case '\"': buffer.append("&quot;"); break;
            case '\'': buffer.append("&#39;");  break;
            case '<':  buffer.append("&lt;");   break;
            case '>':  buffer.append("&gt;");   break;
            default:   buffer.append(1, c);     break;
        }
    }
    return buffer;
}

// Function to log messages
void log_message(const string& message) {
    ofstream log_file("link_budget.log", ios_base::app);
    time_t now = time(0);
    char* dt = ctime(&now);
    log_file << dt << ": " << message << endl;
    log_file.close();
}

// Function to validate input
void validate_input(const string& name, const string& value, double min, double max) {
    if (value.empty()) {
        throw runtime_error(name + " is required.");
    }
    try {
        size_t pos;
        double num_value = stod(value, &pos);
        if (pos != value.size()) {
            throw runtime_error(name + " must be a valid number.");
        }
        if (num_value < min || num_value > max) {
            throw runtime_error(name + " must be between " + to_string(min) + " and " + to_string(max) + ".");
        }
    } catch (const invalid_argument&) {
        throw runtime_error(name + " must be a valid number.");
    } catch (const out_of_range&) {
        throw runtime_error(name + " is out of range.");
    }
}

// Function to get cookie value
string get_cookie(const string& name) {
    string cookies = getenv("HTTP_COOKIE") ? getenv("HTTP_COOKIE") : "";
    size_t pos = cookies.find(name + "=");
    if (pos == string::npos) return "";
    size_t start = pos + name.length() + 1;
    size_t end = cookies.find(";", start);
    return cookies.substr(start, end - start);
}

int main() {
    cout << "Content-type:text/html\r\n";

    // Create session directory if it doesn't exist
    mkdir(SESSION_DIR.c_str(), 0700);

    // Get or create session
    string session_id = get_cookie("session_id");
    Session session(session_id);

    if (session_id.empty() || session.is_expired()) {
        cout << "Set-Cookie: session_id=" << session.get_id() << "; HttpOnly; Secure\r\n";
    }

    cout << "\r\n";

    cout << "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>"
         << "<title>RF Link Budget Result</title>"
         << "<style>body { font-family: Arial, sans-serif; max-width: 600px; margin: 0 auto; padding: 20px; }"
         << "h1, h2 { color: #333; } .error { color: red; }</style></head><body>";

    try {
        map<string, string> input = parse_cgi_input();

        // Validate inputs
        validate_input("Transmit Power", input["tx_power"], -30, 60);
        validate_input("Transmit Antenna Gain", input["tx_gain"], -20, 50);
        validate_input("Free Space Loss", input["free_space_loss"], 0, 200);
        validate_input("Miscellaneous Loss", input["misc_loss"], 0, 50);
        validate_input("Receiver Antenna Gain", input["rx_gain"], -20, 50);
        validate_input("Receiver Loss", input["rx_loss"], 0, 50);

        // Convert to double after validation
        double tx_power = stod(input["tx_power"]);
        double tx_gain = stod(input["tx_gain"]);
        double free_space_loss = stod(input["free_space_loss"]);
        double misc_loss = stod(input["misc_loss"]);
        double rx_gain = stod(input["rx_gain"]);
        double rx_loss = stod(input["rx_loss"]);

        // Calculate received power
        double received_power = tx_power + tx_gain - free_space_loss - misc_loss + rx_gain - rx_loss;
        received_power = round(received_power * 100.0) / 100.0;

        // Log calculation
        log_message("Calculation performed. Result: " + to_string(received_power) + " dBm");

        // Store the result in the session
        session.set("last_calculation", to_string(received_power));

        // Output result
        cout << "<h1>RF Link Budget Result</h1>";
        cout << "<p>Received Power: " << received_power << " dBm</p>";
        
        // Show last calculation
        string last_calculation = session.get("last_calculation", "No previous calculation");
        if (last_calculation != to_string(received_power)) {
            cout << "<p>Previous calculation: " << html_escape(last_calculation) << " dBm</p>";
        }

    } catch (const exception& e) {
        // Log error
        log_message("Error occurred: " + string(e.what()));

        cout << "<h2>Error</h2>";
        cout << "<p class='error'>" << html_escape(e.what()) << "</p>";
    }

    cout << "<p><a href='/index.html'>Go Back</a></p>";
    cout << "</body></html>";

    return 0;
}