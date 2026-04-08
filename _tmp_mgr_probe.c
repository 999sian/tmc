#include <stdio.h>
#include <stddef.h>
#include "include/manager/entitySpawnManager.h"
int main(){
 printf("sizeof Manager=%zu\n", sizeof(Manager));
 printf("sizeof EntitySpawnManager=%zu\n", sizeof(EntitySpawnManager));
 printf("off sound=%zu spawnTimer=%zu unk_3c=%zu flag=%zu\n", offsetof(EntitySpawnManager, sound), offsetof(EntitySpawnManager, spawnTimer), offsetof(EntitySpawnManager, unk_3c), offsetof(EntitySpawnManager, flag));
 return 0;
}
