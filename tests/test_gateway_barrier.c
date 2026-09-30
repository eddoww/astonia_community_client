/* Exercise the actual client transport with short writes and would-block.
 * The linker discards unrelated game functions; no renderer or server is needed. */
#include <assert.h>
#include "../src/client/client.c"

static unsigned char written[64];
static size_t written_size;
static ptrdiff_t send_limit = 7;

ptrdiff_t astonia_net_send(astonia_sock *handle, const void *data, size_t size) {
    assert(handle == (astonia_sock *)1);
    if (send_limit < 0) return -1;
    size_t accepted = size < (size_t)send_limit ? size : (size_t)send_limit;
    assert(written_size + accepted <= sizeof(written));
    memcpy(written + written_size, data, accepted);
    written_size += accepted;
    return (ptrdiff_t)accepted;
}
void addline(const char *format, ...) { (void)format; }
int loading_active(void) { return 0; }
void loading_notice(const char *text) { (void)text; }

int main(void) {
    sock = (astonia_sock *)1;
    sockstate = 4;
    const unsigned char stale[] = {8, 3, 'x', 'y', 0};
    client_send((void *)stale, sizeof(stale));
    assert(outused == sizeof(stale));
    client_gateway_barrier(0x04030201, 0x08070605);
    assert(gateway_waiting && outused == 9);
    client_send((void *)stale, sizeof(stale));
    assert(outused == 9);
    send_limit = -1;
    assert(client_flush_output() == 0 && outused == 9);
    send_limit = 7;
    assert(client_flush_output() == 7);
    assert(client_flush_output() == 2);
    assert(outused == 0 && written_size == 16);
    const unsigned char expected[] = {'U','G','W','1','A','C','K','!',1,2,3,4,5,6,7,8};
    assert(memcmp(written, expected, 16) == 0);
    client_gateway_resume();
    client_send((void *)stale, sizeof(stale));
    assert(!gateway_waiting && outused == sizeof(stale));
    /* A barrier must also flush while initial login animation is pending. */
    sockstate = 3; written_size = 0; send_limit = 16;
    client_gateway_barrier(0x04030201, 0x08070605);
    assert(outused == 0 && written_size == 16);
    puts("PASS gateway barrier drops stale commands, freezes input and handles partial writes");
    return 0;
}
