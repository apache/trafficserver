# Makefile.am for mrtg
#

DEFS += @DEFS@

noinst_DATA = navigation_tree.xml

# C Pre-Processor Program.  This is for using the CPP for things
# -----------------------   other than normal.  (CPP) gets a value
#  which leaves extranious data in the file, and this is undesirable
#  for the kind of text-processing I've got in mind.
# If used, note that it does still leave alot of blank lines where
#  #foo's and #include directives existed in the original text.

CUSTOMCPPFLAGS = "-x c -E -P"

# .xml-specific transforms via cpp-program...with kludge to lose leading blank-lines.
#                                         ...extra-kludgy to be safe if no lbl's!
.xmlcpp.xml:
	$(SHELL) -ec '$(CC) $(CUSTOMCPPFLAGS) $(DEFS) $(INCLUDES) $< \
                     | sed '\''1,/-- AND_ME_TOO --/d'\'' > $@'
