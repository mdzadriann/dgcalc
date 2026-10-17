# ----------------------------
# Makefile Options
# ----------------------------

NAME = DGCALC
DESCRIPTION = "Offline relation graphing"
COMPRESSED = NO
ARCHIVED = NO

CFLAGS = -Wall -Wextra -Werror -Oz
CXXFLAGS = -Wall -Wextra -Werror -Oz

# ----------------------------

include $(shell cedev-config --makefile)
