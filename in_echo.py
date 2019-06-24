

i = 0
while(True):
    r = raw_input()
    if (r == 'end'):
        print "FIM"
        break
    else:
        print "Line", i+1, ":", r
    i += 1

