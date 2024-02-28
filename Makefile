APP=website
TEMPLATES=tmpl/index.html tmpl/yourip.html tmpl/404_page.html tmpl/req2long.html tmpl/howmake.html
CC=cc
CFLAGS=-Wall
RM=rm -f

.PHONY: default
default: dev

$(APP): $(APP).o Makefile compile_tmpl.awk
	$(CC) $(CFLAGS) -o $(APP) $(APP).o
	cp Makefile compile_tmpl.awk static/

$(APP).o: quinelines.gen tmplfuncs.gen

quinelines.gen: $(APP).c
	sed 's|^#include "quinelines.gen"$$|***LINES***|; s/\\/\\\\/g; s/"/\\"/g' $(APP).c | awk '{printf "\"%s\",\n", $$0}' > quinelines.gen

tmplfuncs.gen: $(TEMPLATES)
	$(RM) tmplfuncs.gen
	echo "$(TEMPLATES)" | xargs -n1 awk -f ./compile_tmpl.awk >> tmplfuncs.gen

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
	$(RM) static/Makefile
	$(RM) static/compile_tmpl.awk
