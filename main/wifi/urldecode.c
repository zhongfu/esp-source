#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include <ctype.h>

void urlDecode(char *dest, char *str) {
  //int d = 0; /* whether or not the string is decoded */
  char eStr[] = "00"; /* for a hex code */

  strcpy(dest, str);

  //while(!d) {
    //d = 1;
    int i; /* the counter for the string */

    for(i=0;i<strlen(dest);++i) {

      if(dest[i] == '%') {
        if(dest[i+1] == 0)
          return;

        if(isxdigit(dest[i+1]) && isxdigit(dest[i+2])) {

          //d = 0;

          /* combine the next to numbers into one */
          eStr[0] = dest[i+1];
          eStr[1] = dest[i+2];

          /* convert it to decimal */
          long int x = strtol(eStr, NULL, 16);

          /* remove the hex */
          memmove(&dest[i+1], &dest[i+3], strlen(&dest[i+3])+1);

          dest[i] = x;
        }
      }
      else if(dest[i] == '+') { dest[i] = ' '; }
    }
  //}
}
