include ./MakeFile_Public
BINARY := ../kcp-server
STATICLIB := ../kcp_server.a

SRCDIR := ..
.PHONY: all clean
all: cleantarget $(BINARY)

# Analyze project
CPP_FILES := $(shell find $(SRCDIR) -name "*.cpp")
CPP_FILES_WITHOUT_MAIN := $(shell find $(SRCDIR) -name "*.cpp" | egrep -v 'main.cpp')
INC_DIR   := $(addprefix -I,$(shell find $(SRCDIR) -type d | egrep -v '\.\.$$|\.svn|\.git'))
OBJ_FILES := $(subst /,-,$(subst $(SRCDIR)/,,$(CPP_FILES:%.cpp=%.o)))
OBJ_FILES_WITHOUT_MAIN := $(subst /,-,$(subst $(SRCDIR)/,,$(CPP_FILES_WITHOUT_MAIN:%.cpp=%.o)))
DEP_FILES := $(subst /,-,$(subst $(SRCDIR)/,,$(CPP_FILES:%.cpp=%.d)))

CPPFLAGS += -pipe -std=c++0x -D_SHARED_OBJECT_POOL_USE_TR1 $(INC_DIR)
            
LDFLAGS += -L./

ARFLAGS = cr

# Generate target 
ifeq "$(DEBUG)" "no"
	VERSION_STR = --- This is a RELEASE version!! ---
else
	VERSION_STR = --- This is a DEBUG version!! ---
	CPPFLAGS += -D_DEBUG_
endif

# Generate dependence, including rules to generate object files
%.d:
	@echo Generate $@ ...
	@$(CXX) -MM $(CPPFLAGS) $(SRCDIR)/$(subst -,/,$*).cpp > $@.$$$$; \
	sed 's,.*:,$*.o:,g' < $@.$$$$ >$@; \
	echo '	$(CXX) -c $(CPPFLAGS) $(SRCDIR)/$(subst -,/,$*).cpp -o $*.o' >>$@; \
	echo '$@: $(SRCDIR)/$(subst -,/,$*).cpp' >> $@; \
	rm -f $@.$$$$;

# Include dependence
ifneq ($(MAKECMDGOALS),clean) 
-include $(DEP_FILES) 
endif

ifneq ($(BINARY),)
$(BINARY): $(OBJ_FILES)
#	$(CXX) -Wl,--as-needed $(CPPFLAGS) $^ $(LDFLAGS) -o $@   
#	#$(CXX) $(CPPFLAGS) $^ $(LDFLAGS) -o $@   
	g++ $(CPPFLAGS) $^ $(LDFLAGS) -o $@ -lpthread 
	@echo                                                                   
	@echo $(VERSION_STR) 
	@echo 
endif

ifneq ($(STATICLIB),)
lib : $(OBJ_FILES_WITHOUT_MAIN)
	ar rcs $(STATICLIB) $^
endif

# Clean project
cleantarget:
	-rm -rf $(BINARY)

clean:
	-rm -rf $(BINARY) $(STATICLIB) $(OBJ_FILES) $(DEP_FILES) *.d.* *.d *.o 

