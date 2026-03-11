all: merc integration-test

CC      = gcc
PROF    = 
NOCRYPT =

# Debugging flags possible:  DEBUG_MEM DEBUG_MEM_CORRUPT DEBUG_MEM_DUP_FREE
# DEBUG_MEM is most basic, just checks magic numbers when freeing
# DEBUG_MEM_CORRUPT checks every free block to see if magic numbers ok, every
#                   call to alloc_mem
#		    Also makes calls to void check_free_mem(void) check for
#		    corrupted free memory blocks.
# DEBUG_MEM_DUP_FREE checks to see if freed blocks are overlapping every call
# 		     to mem_alloc.
#

#  -DDEBUG_MEM -DDEBUG_MEM_CORRUPT -DDEBUG_MEM_DUP_FREE
C_FLAGS = -O -g3 -Wall $(PROF) $(NOCRYPT) 
#C_FLAGS = -g3 -Wall ($PROF) $(NOCRYPT)
#-DDEBUG_MEM -DDEBUG_MEM_CORRUPT
L_FLAGS = -O -g $(PROF)
#L_FLAGS = $(PROF)

O_FILES = act_comm.o act_info.o act_move.o act_obj.o act_wiz.o comm.o const.o \
          db.o fight.o handler.o interp.o magic.o magic2.o mob_commands.o mob_prog.o   \
	  save.o special.o update.o board.o areasave.o buildtab.o build.o     \
          write.o act_clan.o buildare.o hunt.o hash.o areachk.o clutch.o      \
          obj_fun.o act_mob.o macros.o trigger.o magic3.o quest.o lists.o  \
          social-edit.o imc.o imc-envy.o imc-comm.o imc-version.o imc-mail.o \
          vampyre.o werewolf.o mount.o pdelete.o wizutil.o ssm.o scheck.o rulers.o \
	  spendqp.o enchant.o

merc: $(O_FILES)
	rm -f merc
	$(CC) $(L_FLAGS) -o merc $(O_FILES) -lm -lcrypt

.c.o: merc.h
	$(CC) -c $(C_FLAGS) $<

scan: scan.o
	rm -f scan
	$(CC) $(L_FLAGS) -o scan scan.o

clean:
	rm -f $(O_FILES) merc ../area/core
	make


integration-test: merc
	python3 tests/integration_login_test.py
	python3 tests/integration_websocket_login_test.py
