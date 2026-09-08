#pragma once

inline int floorDiv(int a, int b)
{
    int q = a / b;
    int r = a % b;
    if (r != 0 && ((r < 0) != (b < 0)))
    {
        q -= 1;
    }
    return q;
}

inline int floorMod(int a, int b)
{
    int r = a % b;
    if (r < 0)
    {
        r += b;
    }
    return r;
}
