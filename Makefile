# Build rapide avec Ninja + ccache (voir build-fast.sh)
# Commandes: make, make clean, make rebuild, make jobs=N

JOBS ?= $(shell nproc)

.PHONY: all clean rebuild jobs

all:
	./build-fast.sh $(JOBS)

clean:
	rm -rf build-ninja

rebuild: clean all

# Pour changer le nombre de jobs: make jobs=4
jobs:
	./build-fast.sh $(jobs)
