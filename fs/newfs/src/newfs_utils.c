#include "../include/newfs.h"
extern struct newfs_super      super; 
extern struct custom_options newfs_options;

/**
 * @brief 驱动读
 * 1 block = 2 io unit
 * @param offset 
 * @param out_content 
 * @param size 
 * @return int 
 */
int newfs_driver_read(int offset, uint8_t *out_content, int size) {
    int      offset_aligned = NEWFS_ROUND_DOWN(offset, NEWFS_BLK_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = NEWFS_ROUND_UP((size + bias), NEWFS_BLK_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    // lseek(SFS_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // read(SFS_DRIVER(), cur, SFS_IO_SZ());
        ddriver_read(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());   // 以io单元为单位
        cur          += NEWFS_IO_SZ();
        size_aligned -= NEWFS_IO_SZ();   
    }
    memcpy(out_content, temp_content + bias, size);
    free(temp_content);
    return NEWFS_ERROR_NONE;
}

/**
 * @brief 驱动写
 * 
 * @param offset 
 * @param in_content 
 * @param size 
 * @return int 
 */
int newfs_driver_write(int offset, uint8_t *in_content, int size) {
    int      offset_aligned = NEWFS_ROUND_DOWN(offset, NEWFS_BLK_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = NEWFS_ROUND_UP((size + bias), NEWFS_BLK_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    newfs_driver_read(offset_aligned, temp_content, size_aligned);  // 读出块
    memcpy(temp_content + bias, in_content, size);                  // 修改写的部分
    
    // lseek(SFS_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // write(SFS_DRIVER(), cur, SFS_IO_SZ());
        ddriver_write(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());  // 以io单元为单位
        cur          += NEWFS_IO_SZ();
        size_aligned -= NEWFS_IO_SZ();   
    }

    free(temp_content);
    return NEWFS_ERROR_NONE;
}

/**
 * @brief 分配一个inode，占用位图
 * 
 * @param dentry 该dentry指向分配的inode
 * @return sfs_inode
 */
struct newfs_inode* newfs_alloc_inode(struct newfs_dentry * dentry) {
    struct newfs_inode* inode;
    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int ino_cursor  = 0;
    boolean is_find_free_entry = FALSE;

    // 在inode位图上寻找未使用的inode
    for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(super.map_inode_blks); 
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前ino_cursor位置空闲 */
                super.map_inode[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = TRUE;           
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }

    if (!is_find_free_entry || ino_cursor == super.max_ino)
        return -NEWFS_ERROR_NOSPACE;
    // 为目录项分配inode节点，并建立他们之间的链接
    inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    inode->ino  = ino_cursor; 
    inode->size = 0;
                                                      /* dentry指向inode */
    dentry->inode = inode;
    dentry->ino   = inode->ino;
                                                      /* inode指回dentry */
    inode->dentry = dentry;
    
    inode->dir_cnt = 0;
    inode->dentrys = NULL;
    for (int i = 0; i < 6; i++)
        inode->block_pointer[i] = 0;

    if (NEWFS_IS_REG(inode)) {
        inode->data = (uint8_t *)malloc(NEWFS_BLKS_SZ(NEWFS_DATA_PER_FILE));    // 这里是为数据在内存中分配空间
    }

    return inode;
}

/**
 * @brief 将内存inode及其下方结构全部刷回磁盘
 * 
 * @param inode 
 * @return int 
 */
int newfs_sync_inode(struct newfs_inode * inode) {
    struct newfs_inode_d  inode_d;
    struct newfs_dentry*  dentry_cursor;
    struct newfs_dentry_d dentry_d;
    int ino             = inode->ino;
    inode_d.ino         = ino;
    inode_d.size        = inode->size;
    memcpy(inode_d.target_path, inode->target_path, MAX_NAME_LEN);
    inode_d.ftype       = inode->dentry->ftype;
    inode_d.dir_cnt     = inode->dir_cnt;
    int offset;
    // 将inode写回磁盘
    if (newfs_driver_write(NEWFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                     sizeof(struct newfs_inode_d)) != NEWFS_ERROR_NONE) {
        // SFS_DBG("[%s] io error\n", __func__);
        return -NEWFS_ERROR_IO;
    }
                                                      /* Cycle 1: 写 INODE */
                                                      /* Cycle 2: 写 数据 */
    int i = 0;
    // 如果是目录
    if (NEWFS_IS_DIR(inode)) {                          
        dentry_cursor = inode->dentrys;
        offset        = NEWFS_DATA_OFS(inode->block_pointer[i]);
        while (dentry_cursor != NULL)
        {
            memcpy(dentry_d.name, dentry_cursor->name, MAX_NAME_LEN);
            dentry_d.ftype = dentry_cursor->ftype;
            dentry_d.ino = dentry_cursor->ino;
            // 将当前目录项写入磁盘
            // 先判断当前块剩余空间是否足够，不够则读取下一块
            if (NEWFS_ROUND_DOWN(offset+sizeof(struct newfs_dentry_d),NEWFS_BLK_SZ()) > NEWFS_ROUND_DOWN(offset,NEWFS_BLK_SZ())){
                if (++i >= NEWFS_DATA_PER_FILE)     // 最多只能有6块
                    return NEWFS_ERROR_UNSUPPORTED;
                offset = NEWFS_DATA_OFS(inode->block_pointer[i]);
            }
            
            if (newfs_driver_write(offset, (uint8_t *)&dentry_d, 
                                 sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE) {
                // SFS_DBG("[%s] io error\n", __func__);
                return -NEWFS_ERROR_IO;                     
            }
            // 递归刷写每一个目录项对应的inode节点
            if (dentry_cursor->inode != NULL) {
                newfs_sync_inode(dentry_cursor->inode);
            }

            dentry_cursor = dentry_cursor->brother;
            offset += sizeof(struct newfs_dentry_d);
            
        }
    }
    else if (NEWFS_IS_REG(inode)) {                               // 如果是文件，直接将inode指向的数据逐块写入磁盘
        while (i < NEWFS_DATA_PER_FILE && inode->block_pointer[i])
        {
            if (newfs_driver_write(NEWFS_DATA_OFS(inode->block_pointer[i]), (inode->data)+i*NEWFS_BLK_SZ(), 
                             NEWFS_BLK_SZ()) != NEWFS_ERROR_NONE) {
            // SFS_DBG("[%s] io error\n", __func__);
            return -NEWFS_ERROR_IO;
            }
        }
        
        
    }
    return NEWFS_ERROR_NONE;
}

/**
 * @brief 为一个inode分配dentry，采用头插法
 * 
 * @param inode 
 * @param dentry 
 * @return int 
 */
int newfs_alloc_dentry(struct newfs_inode* inode, struct newfs_dentry* dentry) {
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
 * @brief 
 * 
 * @param dentry dentry指向ino，读取该inode
 * @param ino inode唯一编号
 * @return struct sfs_inode* 
 */
struct newfs_inode* newfs_read_inode(struct newfs_dentry * dentry, int ino) {
    struct newfs_inode* inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    struct newfs_inode_d inode_d;
    struct newfs_dentry* sub_dentry;
    struct newfs_dentry_d dentry_d;
    int    dir_cnt = 0, i;
    // 通过磁盘驱动来将磁盘中ino号的inode读入内存
    if (newfs_driver_read(NEWFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                        sizeof(struct newfs_inode_d)) != NEWFS_ERROR_NONE) {
        // SFS_DBG("[%s] io error\n", __func__);
        return NULL;                    
    }
    inode->dir_cnt = 0;
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    memcpy(inode->target_path, inode_d.target_path, MAX_NAME_LEN);
    inode->dentry = dentry;
    inode->dentrys = NULL;
    for (i = 0; i < NEWFS_DATA_PER_FILE; i++)
        inode->block_pointer[i] = inode_d.block_pointer[i];

    int k = 0;      // k用于表示数据块号
    // 判断inode的文件类型，如果是目录类型则需要读取每一个目录项并建立连接
    if (NEWFS_IS_DIR(inode)) {
        dir_cnt = inode_d.dir_cnt;

        int offset = NEWFS_DATA_OFS(inode->block_pointer[k]);
        for (i = 0; i < dir_cnt; i++)
        {
            if (NEWFS_ROUND_DOWN(offset+sizeof(struct newfs_dentry_d),NEWFS_BLK_SZ()) > NEWFS_ROUND_DOWN(offset,NEWFS_BLK_SZ())){
                if (++k >= NEWFS_DATA_PER_FILE)     // 最多只能有6块
                    return NEWFS_ERROR_UNSUPPORTED;
                offset = NEWFS_DATA_OFS(inode->block_pointer[k]);
            }
            
            if (newfs_driver_read(offset, 
                                (uint8_t *)&dentry_d, 
                                sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE) {
                // SFS_DBG("[%s] io error\n", __func__);
                return NULL;                    
            }
            offset += sizeof(struct newfs_dentry_d);
            sub_dentry = new_dentry(dentry_d.name, dentry_d.ftype);
            sub_dentry->parent = inode->dentry;
            sub_dentry->ino    = dentry_d.ino; 
            newfs_alloc_dentry(inode, sub_dentry);    // 将sub_dentry加入inode的目录项链表中
        }
    }
    // 如果是文件类型直接读取数据即可。
    else if (NEWFS_IS_REG(inode)) {
        // inode->data = (uint8_t *)malloc(NEWFS_BLKS_SZ(NEWFS_DATA_PER_FILE)); 创建inode时已经开辟空间
        while (k < NEWFS_DATA_PER_FILE && inode->block_pointer[k])
        {
            if (newfs_driver_read(NEWFS_DATA_OFS(inode->block_pointer[i]), (inode->data)+k*NEWFS_BLK_SZ(), 
                             NEWFS_BLK_SZ()) != NEWFS_ERROR_NONE) {
            // SFS_DBG("[%s] io error\n", __func__);
                return -NEWFS_ERROR_IO;
            }
        }
    }
    return inode;
}

int newfs_calc_lvl(const char * path) {
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
/*获取文件名*/
char* newfs_get_fname(const char* path) {
    char ch = '/';
    char *q = strrchr(path, ch) + 1;
    return q;
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
struct newfs_dentry* newfs_lookup(const char * path, boolean* is_find, boolean* is_root) {
    struct newfs_dentry* dentry_cursor = super.root_dentry;
    struct newfs_dentry* dentry_ret = NULL;
    struct newfs_inode*  inode; 
    int   total_lvl = newfs_calc_lvl(path);
    int   lvl = 0;
    boolean is_hit;
    char* fname = NULL;
    char* path_cpy = (char*)malloc(sizeof(path));
    *is_root = FALSE;
    // *is_find = FALSE;
    strcpy(path_cpy, path);

    if (total_lvl == 0) {                           /* 根目录 */
        *is_find = TRUE;
        *is_root = TRUE;
        dentry_ret = super.root_dentry;
    }
    fname = strtok(path_cpy, "/");       
    while (fname)                                   /*从根目录开始查找*/
    {   
        lvl++;
        if (dentry_cursor->inode == NULL) {           /* Cache机制 */
            newfs_read_inode(dentry_cursor, dentry_cursor->ino);
        }

        inode = dentry_cursor->inode;

        if (NEWFS_IS_REG(inode) && lvl < total_lvl) { /*inode中的类型为file，说明找到，返回指向该inode的dentry*/
            // SFS_DBG("[%s] not a dir\n", __func__);
            dentry_ret = inode->dentry;
            break;
        }
        if (NEWFS_IS_DIR(inode)) {                    /*inode中的类型为dir，则需要遍历该inode的目录项链表dentrys*/
            dentry_cursor = inode->dentrys;
            is_hit        = FALSE;

            while (dentry_cursor)
            {
                if (memcmp(dentry_cursor->name, fname, strlen(fname)) == 0) {  /*在所有目录项中找与当前fname相同的*/
                    is_hit = TRUE;
                    break;
                }
                dentry_cursor = dentry_cursor->brother;
            }
            
            if (!is_hit) {                          /*所有目录项都没找到，直接返回指向当前inode的dentry*/
                *is_find = FALSE;
                // SFS_DBG("[%s] not found %s\n", __func__, fname);
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
        dentry_ret->inode = newfs_read_inode(dentry_ret, dentry_ret->ino);    // 找到后顺便把数据读取了
    }
    
    return dentry_ret;
}

/**
 * @brief 挂载sfs, Layout 如下
 * 
 * Layout
 * | Super | Inode Map | Data Map | Inode | Data
 * 
 * IO_SZ * 2 = BLK_SZ
 * 
 * 每个Inode占用一个128B
 * @param options 
 * @return int 
 */
int newfs_mount(struct custom_options options){
    int                     ret = NEWFS_ERROR_NONE;
    int                     driver_fd;
    struct newfs_super_d    newfs_super_d;      /*磁盘超级块*/
    struct newfs_dentry*    root_dentry;        /*根目录*/
    struct newfs_inode*     root_inode;         

    int                     inode_num;          /*inode节点数（文件数）*/
    int                     map_inode_blks;     /*inode位图占的块数*/
    int                     map_data_blks;      /*data位图占的块数*/
    int                     inode_blks;         /*inode占的块数*/

    int                     super_blks;
    boolean                 is_init = FALSE;

    super.is_mounted = FALSE;
    printf("\nmount\n");
    // 打开驱动
    // driver_fd = open(options.device, O_RDWR);
    driver_fd = ddriver_open(options.device);
    // printf("44\n");
    if (driver_fd < 0) {
        return driver_fd;
    }
    
    // 向内存超级块中标记驱动并写入磁盘大小和单次IO大小
    super.fd = driver_fd;
    ddriver_ioctl(NEWFS_DRIVER(), IOC_REQ_DEVICE_SIZE,  &super.sz_disk);
    ddriver_ioctl(NEWFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &super.sz_io);
    
    // 创建根目录项 
    root_dentry = new_dentry("/", NEWFS_DIR);
    // 读取磁盘超级块
    if (newfs_driver_read(NEWFS_SUPER_OFS, (uint8_t *)(&newfs_super_d), 
                        sizeof(struct newfs_super_d)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }   


    // 根据超级块幻数判断是否为第一次启动磁盘，如果是第一次启动磁盘，则需要建立磁盘超级块的布局                                                  /* 读取super */
    if (newfs_super_d.magic_num != NEWFS_MAGIC_NUM) {     /* 幻数无 */
                                                      /* 估算各部分大小 */
        super_blks = 1; /*超级块占一个block*/
        // 索引节点数
        inode_num  =  NEWFS_DISK_SZ() / ((NEWFS_DATA_PER_FILE) * NEWFS_BLK_SZ() + NEWFS_INODE_PER_FILE); // 磁盘大小除以（每个文件数据块数 * 块大小 +每个文件节点占用字节数）；这只是十分粗糙的估算
        // inode位图占用块数
        // map_inode_blks = NEWFS_ROUND_UP(NEWFS_ROUND_UP(inode_num, UINT32_BITS), NEWFS_IO_SZ()) 
        //                  / NEWFS_BLK_SZ();
        map_inode_blks = 1; /*inode位图占用1个块*/
        // inode占的块数
        inode_blks = NEWFS_ROUND_UP(NEWFS_INODE_PER_FILE * inode_num, NEWFS_IO_SZ()) 
                         / NEWFS_BLK_SZ(); 
        // 数据块数
        // int block_num =  NEWFS_DISK_SZ() / NEWFS_BLK_SZ();      
        // 数据位图占用块数                                                      
        // map_data_blks =  NEWFS_ROUND_UP(NEWFS_ROUND_UP(block_num, UINT32_BITS), NEWFS_IO_SZ()) 
        //                  / NEWFS_BLK_SZ();
        map_data_blks = 2;  /*数据位图占用2个块*/

        /* 布局layout */
        newfs_super_d.max_ino = (inode_num - super_blks - map_inode_blks - map_data_blks);              //  
        newfs_super_d.map_inode_offset = NEWFS_SUPER_OFS + NEWFS_BLKS_SZ(super_blks);                   /*inode位图的地址偏移量*/
        newfs_super_d.map_data_offset = newfs_super_d.map_inode_offset + NEWFS_BLKS_SZ(map_inode_blks); /*数据位图的地址偏移量*/
        newfs_super_d.inode_offset = newfs_super_d.map_data_offset + NEWFS_BLKS_SZ(map_data_blks);      /*存放inode起始偏移量*/
        newfs_super_d.data_offset = newfs_super_d.map_inode_offset + NEWFS_BLKS_SZ(inode_blks);         /*存放inode起始偏移量*/
        newfs_super_d.map_inode_blks  = map_inode_blks;
        newfs_super_d.map_data_blks = map_data_blks;
        newfs_super_d.sz_usage = 0;
        // SFS_DBG("inode map blocks: %d\n", map_inode_blks);
        is_init = TRUE;
    }

    // 初始化内存中的超级块
    super.sz_usage   = newfs_super_d.sz_usage;      /* 建立 in-memory 结构 */

    super.max_ino = newfs_super_d.max_ino;

    // 索引节点及索引节点位图部分
    super.map_inode = (uint8_t *)malloc(NEWFS_BLKS_SZ(newfs_super_d.map_inode_blks));
    super.map_inode_blks = newfs_super_d.map_inode_blks;        // inode位图占用块数
    super.map_inode_offset = newfs_super_d.map_inode_offset;    // inode位图的地址
    super.inode_offset = newfs_super_d.inode_offset;            // inode的起始地址
    // 读取inode位图
    if (newfs_driver_read(newfs_super_d.map_inode_offset, (uint8_t *)(super.map_inode), 
                        NEWFS_BLKS_SZ(newfs_super_d.map_inode_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    // 数据块及数据块位图部分
    super.map_data = (uint8_t *)malloc(NEWFS_BLKS_SZ(newfs_super_d.map_data_blks));
    super.map_data_blks = newfs_super_d.map_data_blks;          // 数据块位图占用块数
    super.map_data_offset = newfs_super_d.map_data_offset;      // 数据块位图的地址
    super.data_offset = newfs_super_d.data_offset;            // 数据块的起始地址
    // 读取数据块位图
    if (newfs_driver_read(newfs_super_d.map_data_offset, (uint8_t *)(super.map_data), 
                        NEWFS_BLKS_SZ(newfs_super_d.map_data_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    // 初始化根目录项
    if (is_init) {                                    /* 分配根节点 */
        root_inode = newfs_alloc_inode(root_dentry);    // 为根目录项创建inode节点
        newfs_sync_inode(root_inode);
    }
    
    root_inode            = newfs_read_inode(root_dentry, NEWFS_ROOT_INO);
    root_dentry->inode    = root_inode;
    super.root_dentry = root_dentry;
    super.is_mounted  = TRUE;

    // newfs_dump_map();
    return ret;
}
/**
 * @brief 
 * 
 * @return int 
 */
int newfs_umount() {
    struct newfs_super_d  newfs_super_d; 

    if (!super.is_mounted) {
        return NEWFS_ERROR_NONE;
    }
    // 回收（写回磁盘）
    newfs_sync_inode(super.root_dentry->inode);     /* 从根节点向下刷写节点 */
    // 将内存超级块转化为磁盘超级块                                                
    newfs_super_d.magic_num           = NEWFS_MAGIC_NUM;    /*幻数，标志文件系统已初始化（格式化）*/
    newfs_super_d.max_ino             = super.max_ino;
    newfs_super_d.map_inode_blks      = super.map_inode_blks;
    newfs_super_d.map_inode_offset    = super.map_inode_offset;
    newfs_super_d.map_data_blks       = super.map_data_blks;
    newfs_super_d.map_data_offset     = super.map_data_offset;
    newfs_super_d.data_offset         = super.data_offset;
    newfs_super_d.sz_usage            = super.sz_usage;
    newfs_super_d.inode_offset        = super.inode_offset;
    newfs_super_d.data_offset         = super.data_offset;
    // 写回超级块到磁盘
    if (newfs_driver_write(NEWFS_SUPER_OFS, (uint8_t *)&newfs_super_d, 
                     sizeof(struct newfs_super_d)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }
    // 写回inode位图到磁盘
    if (newfs_driver_write(newfs_super_d.map_inode_offset, (uint8_t *)(super.map_inode), 
                         NEWFS_BLKS_SZ(newfs_super_d.map_inode_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }
    // 释放空间
    free(super.map_inode);

    // 写回数据块位图到磁盘
    if (newfs_driver_write(newfs_super_d.map_data_offset, (uint8_t *)(super.map_data), 
                         NEWFS_BLKS_SZ(newfs_super_d.map_data_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }
    // 释放空间
    free(super.map_data);

    // 关闭驱动
    ddriver_close(NEWFS_DRIVER());

    return NEWFS_ERROR_NONE;
}