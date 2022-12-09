#include "../include/sfs.h"

extern struct sfs_super      sfs_super; 
extern struct custom_options sfs_options;

/**
 * @brief 获取文件名
 * 
 * @param path 
 * @return char* 
 */
char* sfs_get_fname(const char* path) {
    char ch = '/';
    char *q = strrchr(path, ch) + 1;
    return q;
}
/**
 * @brief 计算路径的层级
 * exm: /av/c/d/f
 * -> lvl = 4
 * @param path 
 * @return int 
 */
int sfs_calc_lvl(const char * path) {
    // char* path_cpy = (char *)malloc(strlen(path));
    // strcpy(path_cpy, path);
    char* str = path;
    int   lvl = 0;
    if (strcmp(path, "/") == 0) {
        return lvl;
    }
    while (*str != NULL) {
        if (*str == '/') {
            lvl++;
        }
        str++;
    }
    return lvl;
}
/**
 * @brief 驱动读
 * 
 * @param offset 
 * @param out_content 
 * @param size 
 * @return int 
 */
int sfs_driver_read(int offset, uint8_t *out_content, int size) {
    int      offset_aligned = SFS_ROUND_DOWN(offset, SFS_IO_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = SFS_ROUND_UP((size + bias), SFS_IO_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    // lseek(SFS_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(SFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // read(SFS_DRIVER(), cur, SFS_IO_SZ());
        ddriver_read(SFS_DRIVER(), cur, SFS_IO_SZ());
        cur          += SFS_IO_SZ();
        size_aligned -= SFS_IO_SZ();   
    }
    memcpy(out_content, temp_content + bias, size);
    free(temp_content);
    return SFS_ERROR_NONE;
}
/**
 * @brief 驱动写
 * 
 * @param offset 
 * @param in_content 
 * @param size 
 * @return int 
 */
int sfs_driver_write(int offset, uint8_t *in_content, int size) {
    int      offset_aligned = SFS_ROUND_DOWN(offset, SFS_IO_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = SFS_ROUND_UP((size + bias), SFS_IO_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    sfs_driver_read(offset_aligned, temp_content, size_aligned);
    memcpy(temp_content + bias, in_content, size);
    
    // lseek(SFS_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(SFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // write(SFS_DRIVER(), cur, SFS_IO_SZ());
        ddriver_write(SFS_DRIVER(), cur, SFS_IO_SZ());
        cur          += SFS_IO_SZ();
        size_aligned -= SFS_IO_SZ();   
    }

    free(temp_content);
    return SFS_ERROR_NONE;
}
/**
 * @brief 为一个inode分配dentry，采用头插法
 * 
 * @param inode 
 * @param dentry 
 * @return int 
 */
int sfs_alloc_dentry(struct sfs_inode* inode, struct sfs_dentry* dentry) {
    if (inode->dentrys == NULL) {
        inode->dentrys = dentry;
    }
    else {
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;
    }
    inode->dir_cnt++;
    return inode->dir_cnt;
}
/**
 * @brief 将dentry从inode的dentrys中取出
 * 
 * @param inode 
 * @param dentry 
 * @return int 
 */
int sfs_drop_dentry(struct sfs_inode * inode, struct sfs_dentry * dentry) {
    boolean is_find = FALSE;
    struct sfs_dentry* dentry_cursor;
    dentry_cursor = inode->dentrys;
    
    if (dentry_cursor == dentry) {
        inode->dentrys = dentry->brother;
        is_find = TRUE;
    }
    else {
        while (dentry_cursor)
        {
            if (dentry_cursor->brother == dentry) {
                dentry_cursor->brother = dentry->brother;
                is_find = TRUE;
                break;
            }
            dentry_cursor = dentry_cursor->brother;
        }
    }
    if (!is_find) {
        return -SFS_ERROR_NOTFOUND;
    }
    inode->dir_cnt--;
    return inode->dir_cnt;
}
/**
 * @brief 分配一个inode，占用位图
 * 
 * @param dentry 该dentry指向分配的inode
 * @return sfs_inode
 */
struct sfs_inode* sfs_alloc_inode(struct sfs_dentry * dentry) {
    struct sfs_inode* inode;
    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int ino_cursor  = 0;
    boolean is_find_free_entry = FALSE;

    // 在inode位图上寻找未使用的inode
    for (byte_cursor = 0; byte_cursor < SFS_BLKS_SZ(sfs_super.map_inode_blks); 
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((sfs_super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前ino_cursor位置空闲 */
                sfs_super.map_inode[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = TRUE;           
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }

    if (!is_find_free_entry || ino_cursor == sfs_super.max_ino)
        return -SFS_ERROR_NOSPACE;
    // 为目录项分配inode节点，并建立他们之间的链接
    inode = (struct sfs_inode*)malloc(sizeof(struct sfs_inode));
    inode->ino  = ino_cursor; 
    inode->size = 0;
                                                      /* dentry指向inode */
    dentry->inode = inode;
    dentry->ino   = inode->ino;
                                                      /* inode指回dentry */
    inode->dentry = dentry;
    
    inode->dir_cnt = 0;
    inode->dentrys = NULL;
    
    if (SFS_IS_REG(inode)) {
        inode->data = (uint8_t *)malloc(SFS_BLKS_SZ(SFS_DATA_PER_FILE));
    }

    return inode;
}
/**
 * @brief 将内存inode及其下方结构全部刷回磁盘
 * 
 * @param inode 
 * @return int 
 */
int sfs_sync_inode(struct sfs_inode * inode) {
    struct sfs_inode_d  inode_d;
    struct sfs_dentry*  dentry_cursor;
    struct sfs_dentry_d dentry_d;
    int ino             = inode->ino;
    inode_d.ino         = ino;
    inode_d.size        = inode->size;
    memcpy(inode_d.target_path, inode->target_path, SFS_MAX_FILE_NAME);
    inode_d.ftype       = inode->dentry->ftype;
    inode_d.dir_cnt     = inode->dir_cnt;
    int offset;
    // 将inode写回磁盘
    if (sfs_driver_write(SFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                     sizeof(struct sfs_inode_d)) != SFS_ERROR_NONE) {
        SFS_DBG("[%s] io error\n", __func__);
        return -SFS_ERROR_IO;
    }
                                                      /* Cycle 1: 写 INODE */
                                                      /* Cycle 2: 写 数据 */
    // 如果是目录
    if (SFS_IS_DIR(inode)) {                          
        dentry_cursor = inode->dentrys;
        offset        = SFS_DATA_OFS(ino);
        while (dentry_cursor != NULL)
        {
            memcpy(dentry_d.fname, dentry_cursor->fname, SFS_MAX_FILE_NAME);
            dentry_d.ftype = dentry_cursor->ftype;
            dentry_d.ino = dentry_cursor->ino;
            // 将当前目录项写入磁盘
            if (sfs_driver_write(offset, (uint8_t *)&dentry_d, 
                                 sizeof(struct sfs_dentry_d)) != SFS_ERROR_NONE) {
                SFS_DBG("[%s] io error\n", __func__);
                return -SFS_ERROR_IO;                     
            }
            // 递归刷写每一个目录项对应的inode节点
            if (dentry_cursor->inode != NULL) {
                sfs_sync_inode(dentry_cursor->inode);
            }

            dentry_cursor = dentry_cursor->brother;
            offset += sizeof(struct sfs_dentry_d);
        }
    }
    else if (SFS_IS_REG(inode)) {                               // 如果是文件，直接将inode指向的数据写入磁盘
        if (sfs_driver_write(SFS_DATA_OFS(ino), inode->data, 
                             SFS_BLKS_SZ(SFS_DATA_PER_FILE)) != SFS_ERROR_NONE) {
            SFS_DBG("[%s] io error\n", __func__);
            return -SFS_ERROR_IO;
        }
    }
    return SFS_ERROR_NONE;
}
/**
 * @brief 删除内存中的一个inode， 暂时不释放
 * Case 1: Reg File
 * 
 *                  Inode
 *                /      \
 *            Dentry -> Dentry (Reg Dentry)
 *                       |
 *                      Inode  (Reg File)
 * 
 *  1) Step 1. Erase Bitmap     
 *  2) Step 2. Free Inode                      (Function of sfs_drop_inode)
 * ------------------------------------------------------------------------
 *  3) *Setp 3. Free Dentry belonging to Inode (Outsider)
 * ========================================================================
 * Case 2: Dir
 *                  Inode
 *                /      \
 *            Dentry -> Dentry (Dir Dentry)
 *                       |
 *                      Inode  (Dir)
 *                    /     \
 *                Dentry -> Dentry
 * 
 *   Recursive
 * @param inode 
 * @return int 
 */
int sfs_drop_inode(struct sfs_inode * inode) {
    struct sfs_dentry*  dentry_cursor;
    struct sfs_dentry*  dentry_to_free;
    struct sfs_inode*   inode_cursor;

    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int ino_cursor  = 0;
    boolean is_find = FALSE;

    if (inode == sfs_super.root_dentry->inode) {
        return SFS_ERROR_INVAL;
    }

    if (SFS_IS_DIR(inode)) {
        dentry_cursor = inode->dentrys;
                                                      /* 递归向下drop */
        while (dentry_cursor)
        {   
            inode_cursor = dentry_cursor->inode;
            sfs_drop_inode(inode_cursor);
            sfs_drop_dentry(inode, dentry_cursor);
            dentry_to_free = dentry_cursor;
            dentry_cursor = dentry_cursor->brother;
            free(dentry_to_free);
        }
    }
    else if (SFS_IS_REG(inode) || SFS_IS_SYM_LINK(inode)) {
        for (byte_cursor = 0; byte_cursor < SFS_BLKS_SZ(sfs_super.map_inode_blks); 
            byte_cursor++)                            /* 调整inodemap */
        {
            for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
                if (ino_cursor == inode->ino) {
                     sfs_super.map_inode[byte_cursor] &= (uint8_t)(~(0x1 << bit_cursor));
                     is_find = TRUE;
                     break;
                }
                ino_cursor++;
            }
            if (is_find == TRUE) {
                break;
            }
        }
        if (inode->data)
            free(inode->data);
        free(inode);
    }
    return SFS_ERROR_NONE;
}
/**
 * @brief 
 * 
 * @param dentry dentry指向ino，读取该inode
 * @param ino inode唯一编号
 * @return struct sfs_inode* 
 */
struct sfs_inode* sfs_read_inode(struct sfs_dentry * dentry, int ino) {
    struct sfs_inode* inode = (struct sfs_inode*)malloc(sizeof(struct sfs_inode));
    struct sfs_inode_d inode_d;
    struct sfs_dentry* sub_dentry;
    struct sfs_dentry_d dentry_d;
    int    dir_cnt = 0, i;
    // 通过磁盘驱动来将磁盘中ino号的inode读入内存
    if (sfs_driver_read(SFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                        sizeof(struct sfs_inode_d)) != SFS_ERROR_NONE) {
        SFS_DBG("[%s] io error\n", __func__);
        return NULL;                    
    }
    inode->dir_cnt = 0;
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    memcpy(inode->target_path, inode_d.target_path, SFS_MAX_FILE_NAME);
    inode->dentry = dentry;
    inode->dentrys = NULL;
    // 判断inode的文件类型，如果是目录类型则需要读取每一个目录项并建立连接
    if (SFS_IS_DIR(inode)) {
        dir_cnt = inode_d.dir_cnt;
        for (i = 0; i < dir_cnt; i++)
        {
            if (sfs_driver_read(SFS_DATA_OFS(ino) + i * sizeof(struct sfs_dentry_d), 
                                (uint8_t *)&dentry_d, 
                                sizeof(struct sfs_dentry_d)) != SFS_ERROR_NONE) {
                SFS_DBG("[%s] io error\n", __func__);
                return NULL;                    
            }
            sub_dentry = new_dentry(dentry_d.fname, dentry_d.ftype);
            sub_dentry->parent = inode->dentry;
            sub_dentry->ino    = dentry_d.ino; 
            sfs_alloc_dentry(inode, sub_dentry);    // 将sub_dentry加入inode的目录项链表中
        }
    }
    // 如果是文件类型直接读取数据即可。
    else if (SFS_IS_REG(inode)) {
        inode->data = (uint8_t *)malloc(SFS_BLKS_SZ(SFS_DATA_PER_FILE));
        if (sfs_driver_read(SFS_DATA_OFS(ino), (uint8_t *)inode->data, 
                            SFS_BLKS_SZ(SFS_DATA_PER_FILE)) != SFS_ERROR_NONE) {
            SFS_DBG("[%s] io error\n", __func__);
            return NULL;                    
        }
    }
    return inode;
}
/**
 * @brief 
 * 
 * @param inode 
 * @param dir [0...]
 * @return struct sfs_dentry* 
 */
struct sfs_dentry* sfs_get_dentry(struct sfs_inode * inode, int dir) {
    struct sfs_dentry* dentry_cursor = inode->dentrys;
    int    cnt = 0;
    while (dentry_cursor)
    {
        if (dir == cnt) {
            return dentry_cursor;
        }
        cnt++;
        dentry_cursor = dentry_cursor->brother;
    }
    return NULL;
}
/**
 * @brief 
 * path: /qwe/ad  total_lvl = 2,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry 
 *      3) find qwe's inode     lvl = 2
 *      4) find ad's dentry
 *
 * path: /qwe     total_lvl = 1,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry
 * 
 * @param path 
 * @return struct sfs_inode* 
 */
struct sfs_dentry* sfs_lookup(const char * path, boolean* is_find, boolean* is_root) {
    struct sfs_dentry* dentry_cursor = sfs_super.root_dentry;
    struct sfs_dentry* dentry_ret = NULL;
    struct sfs_inode*  inode; 
    int   total_lvl = sfs_calc_lvl(path);
    int   lvl = 0;
    boolean is_hit;
    char* fname = NULL;
    char* path_cpy = (char*)malloc(sizeof(path));
    *is_root = FALSE;
    strcpy(path_cpy, path);

    if (total_lvl == 0) {                           /* 根目录 */
        *is_find = TRUE;
        *is_root = TRUE;
        dentry_ret = sfs_super.root_dentry;
    }
    fname = strtok(path_cpy, "/");       
    while (fname)                                   /*从根目录开始查找*/
    {   
        lvl++;
        if (dentry_cursor->inode == NULL) {           /* Cache机制 */
            sfs_read_inode(dentry_cursor, dentry_cursor->ino);
        }

        inode = dentry_cursor->inode;

        if (SFS_IS_REG(inode) && lvl < total_lvl) { /*inode中的类型为file，说明找到，返回指向该inode的dentry*/
            SFS_DBG("[%s] not a dir\n", __func__);
            dentry_ret = inode->dentry;
            break;
        }
        if (SFS_IS_DIR(inode)) {                    /*inode中的类型为dir，则需要遍历该inode的目录项链表dentrys*/
            dentry_cursor = inode->dentrys;
            is_hit        = FALSE;

            while (dentry_cursor)
            {
                if (memcmp(dentry_cursor->fname, fname, strlen(fname)) == 0) {  /*在所有目录项中找与当前fname相同的*/
                    is_hit = TRUE;
                    break;
                }
                dentry_cursor = dentry_cursor->brother;
            }
            
            if (!is_hit) {                          /*所有目录项都没找到，直接返回指向当前inode的dentry*/
                *is_find = FALSE;
                SFS_DBG("[%s] not found %s\n", __func__, fname);
                dentry_ret = inode->dentry;
                break;
            }

            if (is_hit && lvl == total_lvl) {
                *is_find = TRUE;
                dentry_ret = dentry_cursor;
                break;
            }
        }
        fname = strtok(NULL, "/"); 
    }

    if (dentry_ret->inode == NULL) {
        dentry_ret->inode = sfs_read_inode(dentry_ret, dentry_ret->ino);    // 找到后顺便把数据读取了
    }
    
    return dentry_ret;
}
/**
 * @brief 挂载sfs, Layout 如下
 * 
 * Layout
 * | Super | Inode Map | Data |
 * 
 * IO_SZ = BLK_SZ
 * 
 * 每个Inode占用一个Blk
 * @param options 
 * @return int 
 */
int sfs_mount(struct custom_options options){
    int                 ret = SFS_ERROR_NONE;
    int                 driver_fd;
    struct sfs_super_d  sfs_super_d; 
    struct sfs_dentry*  root_dentry;
    struct sfs_inode*   root_inode;

    int                 inode_num;
    int                 map_inode_blks;
    
    int                 super_blks;
    boolean             is_init = FALSE;

    sfs_super.is_mounted = FALSE;

    // 打开驱动
    // driver_fd = open(options.device, O_RDWR);
    driver_fd = ddriver_open(options.device);

    if (driver_fd < 0) {
        return driver_fd;
    }

    // 向内存超级块中标记驱动并写入磁盘大小和单次IO大小
    sfs_super.driver_fd = driver_fd;
    ddriver_ioctl(SFS_DRIVER(), IOC_REQ_DEVICE_SIZE,  &sfs_super.sz_disk);
    ddriver_ioctl(SFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &sfs_super.sz_io);
    
    // 创建根目录项 
    root_dentry = new_dentry("/", SFS_DIR);
    // 读取磁盘超级块到内存
    if (sfs_driver_read(SFS_SUPER_OFS, (uint8_t *)(&sfs_super_d), 
                        sizeof(struct sfs_super_d)) != SFS_ERROR_NONE) {
        return -SFS_ERROR_IO;
    }   


    // 根据超级块幻数判断是否为第一次启动磁盘，如果是第一次启动磁盘，则需要建立磁盘超级块的布局                                                  /* 读取super */
    if (sfs_super_d.magic_num != SFS_MAGIC_NUM) {     /* 幻数无 */
                                                      /* 估算各部分大小 */
        super_blks = SFS_ROUND_UP(sizeof(struct sfs_super_d), SFS_IO_SZ()) / SFS_IO_SZ();

        inode_num  =  SFS_DISK_SZ() / ((SFS_DATA_PER_FILE + SFS_INODE_PER_FILE) * SFS_IO_SZ()); // 磁盘大小除以（每个文件数据块数+每个文件节点占用块数）* io大小（这里一块大小为io大小）

        map_inode_blks = SFS_ROUND_UP(SFS_ROUND_UP(inode_num, UINT32_BITS), SFS_IO_SZ()) 
                         / SFS_IO_SZ();                                                         // 所有inode占用块数（这里一块大小为io大小）
        
                                                      /* 布局layout */
        sfs_super.max_ino = (inode_num - super_blks - map_inode_blks); 
        sfs_super_d.map_inode_offset = SFS_SUPER_OFS + SFS_BLKS_SZ(super_blks);
        sfs_super_d.data_offset = sfs_super_d.map_inode_offset + SFS_BLKS_SZ(map_inode_blks);
        sfs_super_d.map_inode_blks  = map_inode_blks;
        sfs_super_d.sz_usage    = 0;
        SFS_DBG("inode map blocks: %d\n", map_inode_blks);
        is_init = TRUE;
    }

    // 初始化内存中的超级块
    sfs_super.sz_usage   = sfs_super_d.sz_usage;      /* 建立 in-memory 结构 */
    
    sfs_super.map_inode = (uint8_t *)malloc(SFS_BLKS_SZ(sfs_super_d.map_inode_blks));
    sfs_super.map_inode_blks = sfs_super_d.map_inode_blks;
    sfs_super.map_inode_offset = sfs_super_d.map_inode_offset;
    sfs_super.data_offset = sfs_super_d.data_offset;

    if (sfs_driver_read(sfs_super_d.map_inode_offset, (uint8_t *)(sfs_super.map_inode), 
                        SFS_BLKS_SZ(sfs_super_d.map_inode_blks)) != SFS_ERROR_NONE) {
        return -SFS_ERROR_IO;
    }
    // 初始化根目录项
    if (is_init) {                                    /* 分配根节点 主要是为了在位图中声明占用 */
        root_inode = sfs_alloc_inode(root_dentry);    // 为目录项创建inode节点
        sfs_sync_inode(root_inode);
    }
    
    root_inode            = sfs_read_inode(root_dentry, SFS_ROOT_INO);
    root_dentry->inode    = root_inode;
    sfs_super.root_dentry = root_dentry;
    sfs_super.is_mounted  = TRUE;

    sfs_dump_map();
    return ret;
}
/**
 * @brief 
 * 
 * @return int 
 */
int sfs_umount() {
    struct sfs_super_d  sfs_super_d; 

    if (!sfs_super.is_mounted) {
        return SFS_ERROR_NONE;
    }
    // 回收（写回磁盘）
    sfs_sync_inode(sfs_super.root_dentry->inode);     /* 从根节点向下刷写节点 */
    // 将内存超级块转化为磁盘超级块                                                
    sfs_super_d.magic_num           = SFS_MAGIC_NUM;
    sfs_super_d.map_inode_blks      = sfs_super.map_inode_blks;
    sfs_super_d.map_inode_offset    = sfs_super.map_inode_offset;
    sfs_super_d.data_offset         = sfs_super.data_offset;
    sfs_super_d.sz_usage            = sfs_super.sz_usage;
    // 写回超级块到磁盘
    if (sfs_driver_write(SFS_SUPER_OFS, (uint8_t *)&sfs_super_d, 
                     sizeof(struct sfs_super_d)) != SFS_ERROR_NONE) {
        return -SFS_ERROR_IO;
    }
    // 写回inode位图到磁盘
    if (sfs_driver_write(sfs_super_d.map_inode_offset, (uint8_t *)(sfs_super.map_inode), 
                         SFS_BLKS_SZ(sfs_super_d.map_inode_blks)) != SFS_ERROR_NONE) {
        return -SFS_ERROR_IO;
    }

    // 释放空间
    free(sfs_super.map_inode);
    // 关闭驱动
    ddriver_close(SFS_DRIVER());

    return SFS_ERROR_NONE;
}
