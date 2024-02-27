CC=cc
APP=website
CFLAGS=-Wall
RM=rm -f

$(APP): $(APP).o
	$(CC) $(CFLAGS) -o $(APP) $(APP).o

$(APP).o: quinelines.gen

quinelines.gen: $(APP).c
	sed 's|^#include "quinelines.gen"$$|***LINES***|; s/\\/\\\\/g; s/"/\\"/g' $(APP).c | awk '{printf "\"%s\",\n", $$0}' > quinelines.gen

.PHONY: dev
dev: $(APP)
	@echo "=== DEV ./$(APP) ==="
	@./$(APP) 8080

.PHONY: run
run: $(APP)
	@echo "=== RUN ./$(APP) ==="
	@./$(APP)

.PHONY: clean
clean:
	$(RM) *.o
	$(RM) *.gen
	$(RM) $(APP)
