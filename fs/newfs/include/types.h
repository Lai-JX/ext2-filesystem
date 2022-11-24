#ifndef _TYPES_H_
#define _TYPES_H_

/******************************************************************************
* SECTION: Type def
*******************************************************************************/
typedef int          boolean;
typedef enum newfs_file_type {
    NEWFS_REG_FILE,
    NEWFS_DIR,
    NEWFS_SYM_LINK
} NEWFS_FILE_TYPE;

/******************************************************************************
* SECTION: Macro
*******************************************************************************/
#define TRUE                    1
#define FALSE                   0
#define UINT32_BITS             32
#define UINT8_BITS              8

/*Error*/
#define NEWFS_ERROR_NONE          0
#define NEWFS_ERROR_NONE          0
#define NEWFS_ERROR_ACCESS        EACCES
#define NEWFS_ERROR_SEEK          ESPIPE     
#define NEWFS_ERROR_ISDIR         EISDIR
#define NEWFS_ERROR_NOSPACE       ENOSPC
#define NEWFS_ERROR_EXISTS        EEXIST
#define NEWFS_ERROR_NOTFOUND      ENOENT
#define NEWFS_ERROR_UNSUPPORTED   ENXIO
#define NEWFS_ERROR_IO            EIO     /* Error Input/Output */
#define NEWFS_ERROR_INVAL         EINVAL  /* Invalid Args */

#define MAX_NAME_LEN              128   
#define NEWFS_DATA_PER_FILE       6     /*一个文件有6块*/
#define NEWFS_INODE_PER_FILE      128   /*一个inode节点占128字节*/

#define NEWFS_MAGIC_NUM           0x52415453  
#define NEWFS_SUPER_OFS           0
#define NEWFS_ROOT_INO            2

/******************************************************************************
* SECTION: Macro Function
*******************************************************************************/
#define NEWFS_IO_SZ()                     (super.sz_io)
#define NEWFS_BLK_SZ()                    (2 * NEWFS_IO_SZ())
#define NEWFS_DISK_SZ()                   (super.sz_disk)
#define NEWFS_DRIVER()                    (super.fd)

#define NEWFS_ROUND_DOWN(value, round)    (value % round == 0 ? value : (value / round) * round)
#define NEWFS_ROUND_UP(value, round)      (value % round == 0 ? value : (value / round + 1) * round)

#define NEWFS_BLKS_SZ(blks)               (blks * NEWFS_BLK_SZ())
#define NEWFS_ASSIGN_FNAME(psfs_dentry, _fname) memcpy(psfs_dentry->name, _fname, strlen(_fname))
// 判断文件类型
#define NEWFS_IS_DIR(pinode)              (pinode->dentry->ftype == NEWFS_DIR)
#define NEWFS_IS_REG(pinode)              (pinode->dentry->ftype == NEWFS_REG_FILE)
#define NEWFS_IS_SYM_LINK(pinode)         (pinode->dentry->ftype == NEWFS_SYM_LINK)
// 获取偏移量
#define NEWFS_INO_OFS(ino)                (super.inode_offset + ino * NEWFS_INODE_PER_FILE)
#define NEWFS_DATA_OFS(ino)               (super.data_offset + NEWFS_BLKS_SZ(ino))
/******************************************************************************
* SECTION: FS Specific Structure - In memory structure
*******************************************************************************/
struct newfs_dentry;
struct newfs_inode;
struct newfs_super;

struct custom_options {
	const char*        device;
};

struct newfs_super {
    int      fd;
    /* TODO: Define yourself */
    
    uint32_t           sz_io;   /*单次io大小*/
    uint32_t           sz_disk; /*磁盘大小*/
    uint32_t           sz_usage;
    
    uint32_t           max_ino;         
    uint8_t*           map_inode;       /*inode的位图*/
    int                map_inode_blks;  /*inode位图所占的数据块*/
    uint32_t           map_inode_offset;/*inode位图的起始地址*/

    /*数据块的位图*/    
    uint8_t*           map_data;        /*数据块的位图*/
    int                map_data_blks;  /*数据块位图所占的数据块*/
    uint32_t           map_data_offset;/*数据块位图的起始地址*/

    uint32_t           inode_offset;    /*索引节点起始地址*/   

    uint32_t           data_offset; /*数据块的起始地址*/

    boolean            is_mounted;  /*是否已装载*/

    struct newfs_dentry* root_dentry; /*根目录*/

};

/*内存中inode数据结构*/
struct newfs_inode {
    uint32_t ino;                                     /* 在inode位图中的下标 */
    /* TODO: Define yourself */                         
    uint32_t           size;                          /* 文件已占用空间 */
    char               target_path[MAX_NAME_LEN];/* store traget path when it is a symlink */
    uint32_t           dir_cnt;
    struct newfs_dentry* dentry;                        /* 指向该inode的dentry */
    struct newfs_dentry* dentrys;                       /* 所有目录项 */
    uint8_t*           data;                            /*默认一个文件数据*/
    uint32_t           block_pointer[6];                          /*数据块指针*/
};

struct newfs_dentry {
    char     name[MAX_NAME_LEN];
    uint32_t ino;
    /* TODO: Define yourself */
    struct newfs_dentry* parent;                        /* 父亲Inode的dentry */
    struct newfs_dentry* brother;                       /* 兄弟 */
    struct newfs_inode*  inode;                         /* 指向inode */
    NEWFS_FILE_TYPE      ftype;
};
/*根据名字和文件类型新建一个目录项*/
static inline struct newfs_dentry* new_dentry(char * fname, NEWFS_FILE_TYPE ftype) {
    struct newfs_dentry * dentry = (struct newfs_dentry *)malloc(sizeof(struct newfs_dentry));
    memset(dentry, 0, sizeof(struct newfs_dentry));
    NEWFS_ASSIGN_FNAME(dentry, fname);
    dentry->ftype   = ftype;
    dentry->ino     = -1;
    dentry->inode   = NULL;
    dentry->parent  = NULL;
    dentry->brother = NULL;                                            
}

/******************************************************************************
* SECTION: FS Specific Structure - Disk structure（磁盘中的数据结构）
*******************************************************************************/
struct newfs_super_d
{
    uint32_t           magic_num;       // 幻数。用于识别文件系统
    uint32_t           sz_usage;
    
    uint32_t           max_ino;         // 最多支持的文件数
    uint32_t           map_inode_blks;  // inode位图占用的块数
    uint32_t           map_inode_offset;

    uint32_t           map_data_blks;  // 数据位图占用的块数
    uint32_t           map_data_offset;

    uint32_t           inode_offset;
    uint32_t           data_offset;
};

struct newfs_inode_d
{
    uint32_t           ino;                           /* 在inode位图中的下标 */
    uint32_t           size;                          /* 文件已占用空间 */
    char               target_path[MAX_NAME_LEN];/* store traget path when it is a symlink */
    int                dir_cnt;
    NEWFS_FILE_TYPE    ftype;   
    uint32_t           block_pointer[6];            /*默认一个文件大小最多为6块*/
};  

struct newfs_dentry_d
{
    char               name[MAX_NAME_LEN];
    NEWFS_FILE_TYPE      ftype;
    uint32_t                ino;                           /* 指向的ino号 */
};  


#endif /* _TYPES_H_ */