#include "connection.h"
#include "datastore.h"
#include <vector>

int main() {
    // Initialize the global data store
    // Any initialization if needed


    // Initialize and run the connection manager
    ConnectionManager connManager;
    connManager.initialize();
    connManager.run();

    return 0;
}
