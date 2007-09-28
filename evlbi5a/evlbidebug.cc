// implementation

static int dbglev_val   = 1;
static int fnthres_val  = 1; // if msglevel>this level => functionnames are printed in DEBUG()

int dbglev_fn( void ) {
    return dbglev_val;
}

int dbglev_fn( int n ) {
    int rv = dbglev_val;
    dbglev_val = n;
    return rv;
}

int fnthres_fn( void ) {
    return fnthres_val;
}

int fnthres_fn( int n ) {
    int rv = fnthres_val;
    fnthres_val = n;
    return rv;
}
