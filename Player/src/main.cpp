#include <player/av_state.h>

#include <iostream>

using std::cout;
using std::endl;

int main(int argc, char* argv[]) {
    int ret{0};
    int flags{0};
    AVState* av_state;

    av_log_set_level(AV_LOG_INFO);
    if (argc < 2) {
        av_log(NULL, AV_LOG_ERROR, "Usage: %s <file>\n", argv[0]);
    }

    cout << "Hello world!" << endl;
    return 0;
}