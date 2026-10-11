#include <memory.h>

int main()
{
    mem::ProcessSession session;
    return session.is_open() ? 1 : 0;
}