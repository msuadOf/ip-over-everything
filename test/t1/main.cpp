#include <stdio.h>
#include <semaphore.h>
#include <pthread.h>
#include <stdint.h>
#define UNUSED(x) (void)(x)

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

    void (*Send)(struct eth_emu *, uint8_t,bool over);
    void (*Receive)(struct eth_emu *, uint8_t,bool over);
    void (*Receive_Callback)(struct eth_emu *from, struct ioe_MAC_t *to, uint8_t data,bool over);
} eth_emu; // 以太网模拟
void Sendc(eth_emu *self, uint8_t d,bool over)
{
#ifdef DEBUG_ETH_EMU
    if (self->Receive == NULL)
    {
        printd("Here! Callback==NULL!!");
        exit(1);
    }
#endif
    self->Receive(self, d,over);

    printf("rec[%d]:%d %s\n", self->id, d,(over)?("over"):(""));
}
void Receivec(eth_emu *self, uint8_t d,bool over)
{
#ifdef DEBUG_ETH_EMU
    if (self->Receive_Callback == NULL)
    {
        printd("Here! Callback==NULL!!");
        exit(1);
    }
#endif
    self->Receive_Callback(self, self->binded_mac, d,0);
}
int Init_eth_emu(eth_emu *t, int id)
{
    t->id = id;
    t->Send = Sendc;
    t->Receive = Receivec;
    return 0;
}
void eth_emu_Init_Recv(eth_emu *t, void (*Receive_Callback)(eth_emu *, struct ioe_MAC_t *, uint8_t,bool))
{

    t->Receive_Callback = Receive_Callback;
}

// ip over everything(ioe)
//====mac=====
typedef struct
{
    uint8_t content[1518];
    int size;

    int send_finish;
    int recv_finish;

    int send_pos;
    int recv_pos;
} ioe_MAC_Message_t;
typedef struct ioe_MAC_Recv_t
{
    ioe_MAC_Message_t *msg;
    void (*Complete_Callback)(ioe_MAC_t *mac);
} ioe_MAC_Recv_t;
typedef struct ioe_MAC_t
{
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
    int *i = &(s->send_pos); // i <- send_pos
    for (*i = 0; *i < s->size-1; (*i)++)
    {
        dev->Send(dev, s->content[*i],0);
    }
    dev->Send(dev, s->content[s->size-1],1);//最后一个给over信号
}

void _ioe_MAC2ETH_Recv_Handle(eth_emu *from, ioe_MAC_t *to, uint8_t d,bool over)
{
    // Assert(from!=NULL,"ji %p %p",from,to);

    ioe_MAC_Message_t *msg = to->recv_handler.msg;
    if (msg->recv_finish == 0)
    {
        msg->content[msg->recv_pos] = d;
        msg->recv_pos++;
        msg->size++;
        //ATT：这里逻辑是，当接受好一帧后，上层如果没有处理这一帧，后面的就会直接丢掉
        if(over==1)//最后一个
        {
            to->recv_handler.Complete_Callback(to);
            msg->recv_finish=1;
        }
    }
}
void ioe_MAC2ETH_Init_Recv(ioe_MAC_t *mac, ioe_MAC_Message_t *recv_buf, void (*MAC_Message_Recv_Complete_Callback)(ioe_MAC_t *mac))
{
    eth_emu *dev = mac->dev;
    mac->recv_handler.Complete_Callback = MAC_Message_Recv_Complete_Callback;
    mac->recv_handler.msg = recv_buf;

    mac->recv_handler.msg->size       =0;
    mac->recv_handler.msg->send_finish=0;
    mac->recv_handler.msg->recv_finish=0;
    mac->recv_handler.msg->send_pos   =0;
    mac->recv_handler.msg->recv_pos   =0;

    eth_emu_Init_Recv(dev, _ioe_MAC2ETH_Recv_Handle);
}

void example_MAC_Message_Recv_Complete_Callback(ioe_MAC_t *mac)
{
    // printf
}

int main()
{
    ioe_MAC_Message_t buf = {0};
    eth_emu eth1 = {0};
    ioe_MAC_Message_t mac_msg = {
        .content = {1, 2},
        .size = 2};
    Init_eth_emu(&eth1, 2);
    ioe_MAC_t mac1 = {0};
    ioe_MAC_Init(&mac1, &eth1);

    ioe_MAC2ETH_Init_Recv(&mac1, &buf, example_MAC_Message_Recv_Complete_Callback);

    ioe_MAC2ETH_Send(&mac1, &mac_msg);
    printf("%d \n",buf.content[0]);
    // sem_t s1;
    // sem_init(&s1,0,0);
    // sem_wait(&s1);
    return 0;
}