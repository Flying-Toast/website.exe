CC=gcc
APP=website
CFLAGS=-Wall
PAGES=$(wildcard pages/*.html)

$(APP): $(APP).o
	$(CC) $(CFLAGS) -o $(APP) $(APP).o

$(APP).o: quinelines.gen $(PAGES:.html=.string.gen)

quinelines.gen: $(APP).c
	sed 's|^#include "quinelines.gen"$$|***LINES***|; s/\\/\\\\/g; s/"/\\"/g' $(APP).c | awk '{printf "\"%s\",\n", $$0}' > quinelines.gen

pages/%.string.gen: pages/%.html
	printf '"' > $@
	sed 's/\\/\\\\/g; s/"/\\"/g' $< | awk '{printf "%s\\n", $$0}' >> $@
	printf '"' >> $@

.PHONY: run
run: $(APP)
	@echo "=== RUN ./$(APP) ==="
	@./$(APP)

.PHONY: clean
clean:
	$(RM) *.o
	$(RM) *.gen
	$(RM) pages/*.gen
	$(RM) $(APP)
