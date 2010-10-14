#include <constraints.h>
using namespace std;

int main() {
    netparms_type      np;
    solution_type      s( solve(0x7f7full) );
    headersearch_type  h4(fmt_mark4, 32);
    headersearch_type  h5(fmt_mark5b);
    constraintset_type cs;

    cout << "Compression solution: " << s.summary() << endl;
    np.set_blocksize(168272);
    try {
        cout << "raw blocks of data over tcp, mtu 1500:" << endl;

        np.set_protocol("tcp");
        np.set_mtu(1500);
        cs = constrain(np);
        cs.validate();
        cout << cs << endl << endl;

        cout << "Id. but now compressed:" << endl;
        cs = constrain(np, s);
        cs.validate();
        cout << cs << endl << endl;

        cout << "Id. over udps, mtu 4470:" << endl;
        np.set_protocol("udps");
        np.set_mtu(4470);
        cs = constrain(np, s);
        cs.validate();
        cout << cs << endl << endl;

        cout << "32 tracks mark4 w/o compression over tcp, mtu 4470, block 39999:" << endl;
        np.set_protocol("tcp");
        np.set_blocksize(39999);
        cs = constrain(np, h4);
        cs.validate();
        cout << cs << endl << endl;

        cout << "32 tracks mark4 w/ compression over tcp, mtu 4470:" << endl;
        cs = constrain(np, h4, s);
        cs.validate();
        cout << cs << endl << endl;

        cout << "mark5b w/o compression over udps, mtu 4470:" << endl;
        np.set_protocol("udps");
        np.set_mtu(4470);
        cs = constrain(np, h5);
        cs.validate();
        cout << cs << endl << endl;

        cout << "id, with compression & MTU 1500:" << endl;
        np.set_mtu(1500);
        cs = constrain(np, h5, s);
        cs.validate();
        cout << cs << endl << endl;

    }
    catch( const exception& e) {
        cout << "AARGH! " << e.what() << endl;
    }
    return 0;
}
