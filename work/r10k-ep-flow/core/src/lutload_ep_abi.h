/* Samsung DRIMe5 public EP ABI; source headers retained with Metol evidence. */
#ifndef NXL_LUTLOAD_EP_ABI_H
#define NXL_LUTLOAD_EP_ABI_H
#include <sys/ioctl.h>
enum ep_k_core_int_sig { EP_K_INT_CORE_RSZ_OUT1_EOF=14 };
enum ep_k_acc_int_sig { EP_K_INT_ACC_3DLUT_RD_FINISH=7 };
struct ep_k_core_intr_wait_info {int timeout_ms;enum ep_k_core_int_sig intr;};
struct ep_k_acc_intr_wait_info {int timeout_ms;unsigned jpeg_err;enum ep_k_acc_int_sig intr;};
#define EP_IOCTL_CORE_INTR_WAIT _IOW('h',6,struct ep_k_core_intr_wait_info)
#define EP_IOCTL_CORE_INTR_WAIT_QUEUE_INIT _IOW('h',7,struct ep_k_core_intr_wait_info)
#define EP_IOCTL_ACC_INTR_WAIT _IOWR('h',8,struct ep_k_acc_intr_wait_info)
#define EP_IOCTL_ACC_INTR_WAIT_QUEUE_INIT _IOW('h',9,struct ep_k_acc_intr_wait_info)
#define EP_IOCTL_CORE_INTR_ENABLE _IOW('h',14,enum ep_k_core_int_sig)
#define EP_IOCTL_ACC_INTR_ENABLE _IOW('h',16,enum ep_k_acc_int_sig)
_Static_assert(sizeof(struct ep_k_core_intr_wait_info)==8,"core ABI");
_Static_assert(sizeof(struct ep_k_acc_intr_wait_info)==12,"acc ABI");
#endif
