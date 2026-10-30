#pragma once
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
struct HostDisplay {
    int w=240,h=135; std::string trace;
    int width() const {return w;} int height() const {return h;}
    void fillScreen(unsigned) {}
    void setTextSize(int) {} void setTextColor(unsigned,unsigned) {}
    void setCursor(int x,int y) {assert(x>=0 && x<w && y>=0 && y<h);}
    void fillRect(int x,int y,int width,int height,unsigned) {assert(x>=0 && y>=0 && width>0 && height>0 && x+width<=w && y+height<=h);}
    void print(const char *value) {trace+=value;}
    void println(const char *value) {trace+=value;trace+='\n';}
    void printf(const char *format,...) {char value[512];va_list a;va_start(a,format);vsnprintf(value,sizeof(value),format,a);va_end(a);trace+=value;}
};
struct HostM5 {HostDisplay Display;};
inline HostM5 M5;
