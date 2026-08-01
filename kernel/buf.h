struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  uint timestamp;       // last use time (for LRU)
  uint bucket;          // which hash bucket this buf is linked in
  struct buf *prev;     // hash bucket list
  struct buf *next;
  uchar data[BSIZE];
};
