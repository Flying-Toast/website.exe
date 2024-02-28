APP=website
TEMPLATES=tmpl/index.html tmpl/yourip.html
CC=cc
CFLAGS=-Wall
RM=rm -f

.PHONY: default
default: dev

$(APP): $(APP).o
	$(CC) $(CFLAGS) -o $(APP) $(APP).o

$(APP).o: quinelines.gen tmplfuncs.gen

quinelines.gen: $(APP).c
	sed 's|^#include "quinelines.gen"$$|***LINES***|; s/\\/\\\\/g; s/"/\\"/g' $(APP).c | awk '{printf "\"%s\",\n", $$0}' > quinelines.gen

tmplfuncs.gen: $(TEMPLATES)
	$(RM) tmplfuncs.gen
	echo "$^" | xargs -n1 awk -f ./compile_tmpl.awk >> tmplfuncs.gen

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
