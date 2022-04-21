CC=gcc
OBJECTS=website.o
CFLAGS=-Wall
EXE=website

$(EXE): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(EXE) $(OBJECTS)

.PHONY: run
run: $(EXE)
	@echo "=== RUN ./$(EXE) ==="
	@./$(EXE)

.PHONY: clean
clean:
	$(RM) *.o
	$(RM) $(EXE)
