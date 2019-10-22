.RECIPEPREFIX +=
CC = g++
CFLAGS = -Wall -lm -lrt -lwiringPi -lpthread

PROG = bin/*
OBJS = obj/*

default:
    mkdir -p bin obj
    $(CC) $(CFLAGS) -c src/GreenHouse.c -o obj/GreenHouse
    $(CC) $(CFLAGS) obj/GreenHouse -o bin/GreenHouse

run:
    sudo ./bin/GreenHouse

clean:
    rm $(PROG) $(OBJS)
