#include <iostream>
#include <fstream>
#include <string>
#include <map>

bool is_word_char(char c) {
  return std::isalnum(c) || c == '_';
}

bool is_safe_char(char c) {
  return std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == ':' || c == '/' || c == '=' || c == '@' ||
         c == '+';
}

std::map<std::string, std::string> load_vars(const char* filename) {
  std::map<std::string, std::string> vars;
  std::ifstream vf(filename);
  if (!vf) {
    std::cerr << "ERROR: Cannot open vars file: " << filename << std::endl;
    std::exit(1);
  }

  std::string line;
  while (std::getline(vf, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    auto pos = line.find('=');
    if (pos == std::string::npos) {
      std::cerr << "ERROR: Invalid line in vars file: " << line << std::endl;
      std::exit(1);
    }

    std::string key = line.substr(0, pos);
    std::string value = line.substr(pos + 1);

    bool valid_key = !key.empty();
    for (char c : key) {
      valid_key &= is_word_char(c);
    }
    if (!valid_key) {
      std::cerr << "ERROR: Invalid variable name: " << key << std::endl;
      std::exit(1);
    }

    bool valid_value = true;
    for (char c : value) {
      valid_value &= is_safe_char(c);
    }
    if (!valid_value) {
      std::cerr << "ERROR: Invalid variable value: " << value << std::endl;
      std::exit(1);
    }

    vars[key] = value;
  }
  return vars;
}

int main(int argc, char* argv[]) {
  bool validate = true;
  int arg_offset = 0;

  if (argc >= 2 && std::string(argv[1]) == "--no-validate") {
    validate = false;
    arg_offset = 1;
  }

  if (argc != 4 + arg_offset) {
    std::cerr << "Usage: " << argv[0] << " [--no-validate] <template> <vars> <output>" << std::endl;
    return 1;
  }

  // Load vars
  std::map<std::string, std::string> vars = load_vars(argv[2 + arg_offset]);
  if (vars.empty()) {
    std::cerr << "ERROR: No valid variables loaded from: " << argv[2 + arg_offset] << std::endl;
    return 1;
  }

  // Load template (stdin or file)
  std::string content;
  if (std::string(argv[1 + arg_offset]) == "-") {
    content.assign((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
  } else {
    std::ifstream tf(argv[1 + arg_offset]);
    if (!tf) {
      std::cerr << "ERROR: Cannot open template file: " << argv[1 + arg_offset] << std::endl;
      return 1;
    }
    content.assign((std::istreambuf_iterator<char>(tf)), std::istreambuf_iterator<char>());
  }

  // Single-pass: iterate through text and replace $KEY
  std::string result;
  result.reserve(content.length() * 2);

  for (size_t i = 0; i < content.length();) {
    if (content[i] != '$') {
      result += content[i];
      i++;
      continue;
    }
    // Find variable name
    size_t start = i + 1;
    size_t end = start;
    while (end < content.length() && is_word_char(content[end])) {
      end++;
    }

    std::string var_name = content.substr(start, end - start);
    auto it = vars.find(var_name);

    if (it != vars.end()) {
      // Replace with value
      result += it->second;
      i = end;  // Skip past the variable
    } else {
      // Variable not found
      if (validate) {
        std::cerr << "ERROR: Undefined variable $" << var_name << std::endl;
        return 1;
      }
      result += content[i];
      i++;
    }
  }

  // Write output (stdout or file)
  if (std::string(argv[3 + arg_offset]) == "-") {
    std::cout << result;
  } else {
    std::ofstream of(argv[3 + arg_offset]);
    if (!of) {
      std::cerr << "ERROR: Cannot create output file: " << argv[3 + arg_offset] << std::endl;
      return 1;
    }
    of << result;
  }
  return 0;
}