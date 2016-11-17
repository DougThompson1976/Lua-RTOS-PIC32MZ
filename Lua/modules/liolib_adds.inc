#include "whitecat.h"
#include "lauxlib.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h> 
#include <limits.h>
#include <stdio.h>
#include <sys/syslog.h>

#include <sys/drivers/uart.h>

#define l_getc(f)		getc(f)
#define l_lockfile(f)   ((void)0)
#define l_unlockfile(f)	((void)0)

void tty_lock();
void tty_unlock();

static int read_line (lua_State *L, FILE *f, int chop) {
  luaL_Buffer b;
  int c = '\0';
  luaL_buffinit(L, &b);
  
  int nl = '\n';
  
  if (f == stdin) {
      nl = '\r';
  }
  
  while (c != EOF && c != nl) {  /* repeat until end of line */
    char *buff = luaL_prepbuffer(&b);  /* pre-allocate buffer */
    int i = 0;
    l_lockfile(f);  /* no memory errors can happen inside the lock */
    while (i < LUAL_BUFFERSIZE && (c = l_getc(f)) != EOF && c != nl) {
      buff[i++] = c;
    
      if (f == stdin) {
          putc(c, stdout);
      }
    }
    
    l_unlockfile(f);
    luaL_addsize(&b, i);
  }
  if (!chop && c == nl)  /* want a newline and have one? */
    luaL_addchar(&b, c);  /* add ending newline to result */
  luaL_pushresult(&b);  /* close buffer */
  /* return ok if read something (either a newline or something else) */
  return (c == nl || lua_rawlen(L, -1) > 0);
}

static int f_receive (lua_State *L) {
    const char *filename = luaL_optstring(L, 1, "");
    unsigned int i;
    int done;

    unsigned char chunk[255];
    unsigned char *cchunk;
    unsigned char chunk_size;
  
    if (strlen(filename) == 0) return 0;

    FILE *f= fopen(filename, "w");
    if (f) {
        // Lock tty, avoid other threads to write to console
        tty_lock();
        
        // Clear received buffer
        uart_consume(CONSOLE_UART);

        // Send 'C' for start
        uart_write(CONSOLE_UART, 'C');
        uart_write(CONSOLE_UART, '\n');

        done = 0;

        for(;;) {
            // Wait for chunk size
            if (!uart_read(CONSOLE_UART, (char *)&chunk_size, 2000)) {
                break;
            }

            // More chunks?
            if (chunk_size == 0) {
                done = 1;
                break;
            }

            // Read chunk
            cchunk = chunk;
            for(i=0; i < chunk_size; i++) {
                if (!uart_read(CONSOLE_UART, (char *)cchunk++, 2000)) {
                    break;
                }
            }

            // Wrhite chunk to disk
            fwrite(chunk,chunk_size,1,f);

            // Send 'C' for start
            uart_write(CONSOLE_UART, 'C');
            uart_write(CONSOLE_UART, '\n');
        }
                
        fclose(f);
        
        if (!done) {
            // Unlock tty, permit other threads to write to console
            tty_unlock();
            
            return luaL_error(L, "timeout");
        }

        // Unlock tty, permit other threads to write to console
        tty_unlock();
    } else {
        return luaL_error(L, strerror(errno));
    }
    
    lua_pushboolean(L, 1);
    return 1;
}

static int f_send (lua_State *L) {
    const char *filename = luaL_optstring(L, 1, "");
    int i;
    int done;
    int error;
    char c;

    unsigned char chunk[255];
    unsigned char *cchunk;
    unsigned char chunk_size;
  
    if (strlen(filename) == 0) return 0;

    FILE *f= fopen(filename, "r");
    if (f) {
        // Lock tty, avoid other threads to write to console
        tty_lock();
       
        done = 1;
        error = 0;
        while (!feof(f)) {
            // Read next chunk
            chunk_size = 0;
            cchunk = chunk;
            for(i=0; i < 255; i++) {
                if (fread(cchunk++,1,1,f) == 1) {
                    chunk_size++;
                }
            }
            
            // Wait for C\n
            if (!uart_read(CONSOLE_UART, &c, 2000)) {done = 0; break;}
            if (c != 'C') {done = 0; error = 1; break;}
            
            if (!uart_read(CONSOLE_UART, &c, 2000)) {done = 0; break;}
            if (c != '\n') {done = 0; error = 1; break;}
            
            // Send chunk size
            uart_write(CONSOLE_UART, chunk_size);

            // Send chunk
            cchunk = chunk;
            while(chunk_size--) {
                uart_write(CONSOLE_UART, *cchunk++);
            }  
        }
                
        fclose(f);
        
        if (done){
            // Send last chunk with size 0

            // Wait for C\n
            if (!uart_read(CONSOLE_UART, &c, 2000)) {tty_unlock();return luaL_error(L, "timeout");}
            if (c != 'C') {tty_unlock();return luaL_error(L, "unnexpected input");}
            
            if (!uart_read(CONSOLE_UART, &c, 2000)) {tty_unlock();return luaL_error(L, "timeout");}
            if (c != '\n') {tty_unlock();return luaL_error(L, "unnexpected input");}
            
            // Send chunk size
            chunk_size = '\0';
            uart_write(CONSOLE_UART, chunk_size);
        } else {
            tty_unlock();
            if (error) {
            	return luaL_error(L, "unnexpected input");
            } else {
            	return luaL_error(L, "timeout");
            }
        }
                
        // Unlock tty, permit other threads to write to console
        tty_unlock();
    } else {
        return luaL_error(L, strerror(errno));
    }
    
    return 0;
}

#undef l_getc
#undef l_lockfile
#undef l_unlockfile