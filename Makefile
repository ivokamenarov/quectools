all: queccli quecd queccom

queccli: queccli.c
	gcc -Wall -O3 -s -o queccli queccli.c -lreadline

quecd: quecd.c
	gcc -Wall -O3 -s -o quecd quecd.c -lpthread

queccom: queccom.c
	gcc -Wall -O3 -s -o queccom queccom.c

clean:
	rm -f core *.o queccli quecd queccom
