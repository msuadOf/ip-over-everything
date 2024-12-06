#include <stdio.h>
#include <semaphore.h>
#include <pthread.h>
#include <stdint.h>
#define UNUSED(x) (void)(x)
#define println(format, ...) \
    printf(format "\n",      \
           ##__VA_ARGS__)
#define CONFIG_IOE_Preamble_Len 7
#define DEBUG

#ifdef DEBUG
#include <stdlib.h>
#define DEBUG_ETH_EMU

#define _printd printf
#define printd(format, ...)            \
    _printd("[%s:%d %s] " format "\n", \
            __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#include <assert.h>
#define Assert(cond, format, ...)                                          \
    do                                                                     \
    {                                                                      \
        if (!(cond))                                                       \
        {                                                                  \
            (fflush(stdout), fprintf(stderr, format "\n", ##__VA_ARGS__)); \
            assert(cond);                                                  \
        }                                                                  \
    } while (0)
#define INIT_VAR(x) ((x) = {0})
#endif
// 创建无名信号量
// int sem_init(sem_t *sem,int pshared,unsigned int value);
class eth_emu2
{
public:
    uint8_t buf[200];
    void (*Receive)(uint8_t);
    void Send(uint8_t)
    {
        this->buf[0] = 0;
    }
};

typedef struct eth_emu
{
    int id;
    struct ioe_MAC_t *binded_mac;

    void (*Send)(struct eth_emu *, uint8_t, bool over);
    void (*Receive)(struct eth_emu *, uint8_t, bool over);
    void (*Receive_Callback)(struct eth_emu *from, struct ioe_MAC_t *to, uint8_t data, bool data_en, bool over);
} eth_emu; // 以太网模拟
void Sendc(eth_emu *self, uint8_t d, bool over)
{
#ifdef DEBUG_ETH_EMU
    if (self->Receive == NULL)
    {
        printd("Here! Callback==NULL!!");
        exit(1);
    }
#endif
    self->Receive(self, d, over);

    printf("rec[%d]:0x%x %s\n", self->id, d, (over) ? ("over") : (""));
}
void Receivec(eth_emu *self, uint8_t d, bool over)
{
#ifdef DEBUG_ETH_EMU
    if (self->Receive_Callback == NULL)
    {
        printd("Here! Callback==NULL!!");
        exit(1);
    }
#endif
    self->Receive_Callback(self, self->binded_mac, d, 1, over);
}
int Init_eth_emu(eth_emu *t, int id)
{
    t->id = id;
    t->Send = Sendc;
    t->Receive = Receivec;
    return 0;
}
void eth_emu_Init_Recv(eth_emu *t, void (*Receive_Callback)(eth_emu *, struct ioe_MAC_t *, uint8_t, bool, bool))
{

    t->Receive_Callback = Receive_Callback;
}

// ip over everything(ioe)
//====mac=====
typedef enum
{
    IDLE = 0,
    PREAMBLE,
    FRAME,
    FINISH,
    ABORT
} IOE_MAC_MESSAGE_STATE;
typedef struct
{
    uint8_t content[1518];
    int size;

    IOE_MAC_MESSAGE_STATE send_state;
    IOE_MAC_MESSAGE_STATE recv_state;

    int send_pos;
    int recv_pos;

    bool _recv_esc;
    int _recv_pre_cnt;
} ioe_MAC_Message_t;
typedef struct
{
    uint8_t a[6];
} ioe_MAC_Addr_t;
typedef union
{
    ioe_MAC_Message_t msg;
    struct
    {
        uint8_t DA[6];
        uint8_t RA[6];
        uint8_t Type[2];
        uint8_t Data[1500];
        uint8_t FCS[4];
    } info;
    struct
    {
        ioe_MAC_Addr_t DA[6];
        ioe_MAC_Addr_t RA[6];
        uint8_t Type[2];
        uint8_t Data[1500];
        uint8_t FCS[4];
    } info2;
} ioe_MAC_Message_identify_union;
typedef struct ioe_MAC_Recv_t
{
    ioe_MAC_Message_t *msg;
    void (*Complete_Callback)(ioe_MAC_t *mac);
} ioe_MAC_Recv_t;
typedef struct ioe_MAC_t
{
    ioe_MAC_Addr_t addr;
    eth_emu *dev;
    ioe_MAC_Recv_t recv_handler;
} ioe_MAC_t;
void ioe_MAC_Init(ioe_MAC_t *mac, eth_emu *dev)
{
    mac->dev = dev;
    dev->binded_mac = mac;
}

void ioe_MAC2ETH_Send(ioe_MAC_t *mac, ioe_MAC_Message_t *s)
{
    eth_emu *dev = mac->dev;
    for (int i = 0; i < CONFIG_IOE_Preamble_Len; i++)
    {
        dev->Send(dev, 0xAA, 0); // 前导码
    }
    dev->Send(dev, 0xAB, 0); // 界定符1010 1011
    int *i = &(s->send_pos); // i <- send_pos
    for (*i = 0; *i < s->size; (*i)++)
    {
        uint8_t d = s->content[*i];
        if (d == (0xAB & 0xFF) || d == '\\')
        {
            dev->Send(dev, '\\', (*i) == (s->size - 1));
        }
        dev->Send(dev, d, (*i) == (s->size - 1)); // 最后一个给over信号
    }
}

void _ioe_MAC2ETH_Recv_Handle(eth_emu *from, ioe_MAC_t *to, uint8_t d, bool d_en, bool over)
{
    // Assert(from!=NULL,"ji %p %p",from,to);
    ioe_MAC_Message_t *msg = to->recv_handler.msg;
    IOE_MAC_MESSAGE_STATE *state = &msg->recv_state;

    switch (msg->recv_state)
    {
    case IDLE:
        if (d == 0xAA)
            msg->_recv_pre_cnt++;
        if (msg->_recv_pre_cnt == 7)
            *state = PREAMBLE;
        break;
    case PREAMBLE:
        if (d == 0xAB)
            *state = FRAME;
        break;
    case FRAME:
    {

        if (d == '\\' && msg->_recv_esc == 0)
        {
            msg->_recv_esc = 1;
            goto msg_end;
        }
        else if (msg->_recv_esc == 1)
        {
            goto msg_end;
        }
        else if (d != '\\' && msg->_recv_esc == 0)
        {
            /* pass */
            if (d == 0xAB)
                goto msg_end;
            else
                goto msg_start;
        }
    msg_start:
        msg->content[msg->recv_pos] = d;
        msg->recv_pos++;
        msg->size++;
    msg_end:
        // ATT：这里逻辑是，当接受好一帧后，上层如果没有处理这一帧，后面的就会直接丢掉
        if (over == 1) // 最后一个
        {
            to->recv_handler.Complete_Callback(to);
            *state = FINISH;
        }
    };
    break;
    case FINISH:;
    case ABORT:;
    default:
        break;
    }
}
void ioe_MAC2ETH_Init_Recv(ioe_MAC_t *mac, ioe_MAC_Message_t *recv_buf, void (*MAC_Message_Recv_Complete_Callback)(ioe_MAC_t *mac))
{
    eth_emu *dev = mac->dev;
    mac->recv_handler.Complete_Callback = MAC_Message_Recv_Complete_Callback;
    mac->recv_handler.msg = recv_buf;

    *mac->recv_handler.msg = (ioe_MAC_Message_t){0};

    eth_emu_Init_Recv(dev, _ioe_MAC2ETH_Recv_Handle);
}
ioe_MAC_Message_t *_ioe_MAC_Message_Gen(ioe_MAC_Message_t *empty_msg, ioe_MAC_Addr_t da, ioe_MAC_Addr_t ra, uint8_t *data, int data_len)
{
    *empty_msg = (ioe_MAC_Message_t){0};
    ioe_MAC_Message_identify_union *a = (ioe_MAC_Message_identify_union *)empty_msg;
    ioe_MAC_Addr_t *pda = &da, *pra = &ra;
    *((uint32_t *)&(((uint32_t *)(a->info.DA))[0])) = *((uint32_t *)&(((uint32_t *)(pda))[0]));
    *((uint16_t *)&(((uint32_t *)(a->info.DA))[1])) = *((uint16_t *)&(((uint32_t *)(pda))[1]));
    *((uint32_t *)&(((uint32_t *)(a->info.RA))[0])) = *((uint32_t *)&(((uint32_t *)(pra))[0]));
    *((uint16_t *)&(((uint32_t *)(a->info.RA))[1])) = *((uint16_t *)&(((uint32_t *)(pra))[1]));
    empty_msg->size = 6 + 6 + 2;
    for (int i = 0; i < data_len; i++)
    {
        empty_msg->content[6 + 6 + 2 + i] = data[i];
    }
    empty_msg->size += data_len;

    empty_msg->size += 4;
    return empty_msg;
}
void print_mac_msg(ioe_MAC_Message_t *msg)
{
    ioe_MAC_Message_identify_union a = {.msg = *msg};
    uint8_t *DA = a.info.DA, *RA = a.info.RA, *Type = a.info.Type, *Data = a.info.Data;
    println("DA(mac):%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", DA[0], DA[1], DA[2], DA[3], DA[4], DA[5]);
    println("RA(mac):%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", RA[0], RA[1], RA[2], RA[3], RA[4], RA[5]);
    println("Type:0x%02hhx%02hhx", Type[0], Type[1]);
    println("%s", "Data:");
    for (int i = 0; i < msg->size - 18; i++)
    {
        printf("%02X ", Data[i]);
    }
    println();
}
void example_MAC_Message_Recv_Complete_Callback(ioe_MAC_t *mac)
{
    // printf
    print_mac_msg(mac->recv_handler.msg);
}

static const unsigned int crc32tab[] = {
    0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL,
    0x076dc419L, 0x706af48fL, 0xe963a535L, 0x9e6495a3L,
    0x0edb8832L, 0x79dcb8a4L, 0xe0d5e91eL, 0x97d2d988L,
    0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L, 0x90bf1d91L,
    0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
    0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L,
    0x136c9856L, 0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL,
    0x14015c4fL, 0x63066cd9L, 0xfa0f3d63L, 0x8d080df5L,
    0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L, 0xa2677172L,
    0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
    0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L,
    0x32d86ce3L, 0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L,
    0x26d930acL, 0x51de003aL, 0xc8d75180L, 0xbfd06116L,
    0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L, 0xb8bda50fL,
    0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
    0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL,
    0x76dc4190L, 0x01db7106L, 0x98d220bcL, 0xefd5102aL,
    0x71b18589L, 0x06b6b51fL, 0x9fbfe4a5L, 0xe8b8d433L,
    0x7807c9a2L, 0x0f00f934L, 0x9609a88eL, 0xe10e9818L,
    0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
    0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL,
    0x6c0695edL, 0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L,
    0x65b0d9c6L, 0x12b7e950L, 0x8bbeb8eaL, 0xfcb9887cL,
    0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L, 0xfbd44c65L,
    0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
    0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL,
    0x4369e96aL, 0x346ed9fcL, 0xad678846L, 0xda60b8d0L,
    0x44042d73L, 0x33031de5L, 0xaa0a4c5fL, 0xdd0d7cc9L,
    0x5005713cL, 0x270241aaL, 0xbe0b1010L, 0xc90c2086L,
    0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
    0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L,
    0x59b33d17L, 0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL,
    0xedb88320L, 0x9abfb3b6L, 0x03b6e20cL, 0x74b1d29aL,
    0xead54739L, 0x9dd277afL, 0x04db2615L, 0x73dc1683L,
    0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
    0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L,
    0xf00f9344L, 0x8708a3d2L, 0x1e01f268L, 0x6906c2feL,
    0xf762575dL, 0x806567cbL, 0x196c3671L, 0x6e6b06e7L,
    0xfed41b76L, 0x89d32be0L, 0x10da7a5aL, 0x67dd4accL,
    0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
    0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L,
    0xd1bb67f1L, 0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL,
    0xd80d2bdaL, 0xaf0a1b4cL, 0x36034af6L, 0x41047a60L,
    0xdf60efc3L, 0xa867df55L, 0x316e8eefL, 0x4669be79L,
    0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
    0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL,
    0xc5ba3bbeL, 0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L,
    0xc2d7ffa7L, 0xb5d0cf31L, 0x2cd99e8bL, 0x5bdeae1dL,
    0x9b64c2b0L, 0xec63f226L, 0x756aa39cL, 0x026d930aL,
    0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
    0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L,
    0x92d28e9bL, 0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L,
    0x86d3d2d4L, 0xf1d4e242L, 0x68ddb3f8L, 0x1fda836eL,
    0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L, 0x18b74777L,
    0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
    0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L,
    0xa00ae278L, 0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L,
    0xa7672661L, 0xd06016f7L, 0x4969474dL, 0x3e6e77dbL,
    0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L, 0x37d83bf0L,
    0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
    0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L,
    0xbad03605L, 0xcdd70693L, 0x54de5729L, 0x23d967bfL,
    0xb3667a2eL, 0xc4614ab8L, 0x5d681b02L, 0x2a6f2b94L,
    0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL, 0x2d02ef8dL};

static unsigned int crc32(const unsigned char *buf, unsigned int size)
{
    unsigned int i, crc;
    crc = 0xFFFFFFFF;

    for (i = 0; i < size; i++)
        crc = crc32tab[(crc ^ buf[i]) & 0xff] ^ (crc >> 8);

    return crc ^ 0xFFFFFFFF;
}
int main()
{
    ioe_MAC_Message_t buf = {0};
    eth_emu eth1 = {0};
    ioe_MAC_Message_t mac_msg = {
        .content = {'\\', 1, 2},
        .size = 5};
    uint8_t buf1[1024] = {1, 2, 3, 4, 5, 6, 7, 8, 9}; //,macadd1[6],macadd2[6];
    _ioe_MAC_Message_Gen(&mac_msg,
                         (ioe_MAC_Addr_t){1, 2, 3, 4, 5, 6},
                         (ioe_MAC_Addr_t){2, 3, 4, 5, 6, 7}, buf1, 10);
    Init_eth_emu(&eth1, 2);
    ioe_MAC_t mac1 = {0};
    ioe_MAC_Init(&mac1, &eth1);

    ioe_MAC2ETH_Init_Recv(&mac1, &buf, example_MAC_Message_Recv_Complete_Callback);

    ioe_MAC2ETH_Send(&mac1, &mac_msg);
    //ioe_MAC2ETH_Send(&mac1, &mac_msg);

    // print_mac_msg(&mac_msg);
    printf("%d \n", buf.content[0]);
    // sem_t s1;
    // sem_init(&s1,0,0);
    // sem_wait(&s1);
    return 0;
}