#!/usr/bin/python

print "#define VALID_KEY_CHAR 1"
print "#define VALID_LOCALE_CHAR 2"
print "guchar valid[256] = {",
for i in range(256):
    val = 0;
    if (((i>=ord('a')) & (i<=ord('z'))) |
        ((i>=ord('A')) & (i<=ord('Z'))) |
        ((i>=ord('0')) & (i<=ord('9'))) |
        (i == ord('-'))):
        val = val + 1;
    if (((i>=ord('a')) & (i<=ord('z'))) |
        ((i>=ord('A')) & (i<=ord('Z'))) |
        ((i>=ord('0')) & (i<=ord('9'))) |
        (i == ord('-')) |
        (i == ord('_')) |
        (i == ord('.'))):
        val = val + 2;
         
    if i%16 == 0:
        print "\n  ",
    print hex(val),
    print ",",

print "\n};"
    
    
