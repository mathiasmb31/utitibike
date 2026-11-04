# Build rapide avec Ninja + ccache (voir build-fast.sh)
# Commandes: make, make clean, make rebuild, make jobs=N

# Plafond mémoire (hôte 11,5 Go RAM, crash OOM 2026-11-04) : 4 jobs max, surcharge via `make jobs=N`
JOBS ?= 4

.PHONY: all clean rebuild jobs

all:
	./build-fast.sh $(JOBS)

clean:
	rm -rf build-ninja

rebuild: clean all

# Pour changer le nombre de jobs: make jobs=4
jobs:
	./build-fast.sh $(jobs)
