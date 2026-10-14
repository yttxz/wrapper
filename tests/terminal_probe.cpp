#include "lite/login_input.h"
int main(int argc, char**) {
    try {
        std::string value = login_input::hidden_line("Password: ", argc > 1 ? 8 : 1023);
        bool ok = value == "fixture-password";
        login_input::wipe(value);
        return ok ? 0 : 2;
    } catch (const login_input::Error&) { return 1; }
}
