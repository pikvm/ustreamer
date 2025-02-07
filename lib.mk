define optbool
$(filter $(shell echo $(1) | tr A-Z a-z), yes on 1)
endef
