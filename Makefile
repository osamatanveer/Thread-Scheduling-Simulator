schedule: schedule.c
	gcc -pthread -o schedule *.c -lm
clean:
	rm schedule