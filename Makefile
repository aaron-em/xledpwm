PROG=xledpwm

xledpwm:
	gcc -Wall -lX11 -o $(PROG) $(PROG).c

clean:
	rm -f $(PROG)
