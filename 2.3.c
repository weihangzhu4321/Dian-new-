#include <ncurses.h>
#include <stdio.h>        
#include <stdlib.h>       
#include <string.h>
#include <errno.h>

static char **line_content  = NULL;    //行内容
static int    line_count    = 0;       //行数

//cur_y 与 cur_x 是“文本中位置”,不再是屏幕坐标
static int cur_y = 0;             //行号 (0 .. line_count-1)
static int cur_x = 0;             //列号 (0 .. strlen(line_content[cur_y]))
static int top_row = 0;           //当前屏幕顶行对应第几个视觉行
static int want_x = 0;            //即原本的屏幕列

/*--------状态栏---------*/
static const char *editor_state = "Normal";       //编辑器状态（这一节只有这一种，随便命名）
static int modified = 0;                          //文件有无未保存修改


/*----------函数声明----------------*/
int initial(void);                        //初始化ncurses
int cursor(int max_y,int max_x);        //光标定位
int editor(const char *filename);             //主要实现，按光标位置滚动，排版，显示，并等待下一个
int add_line(const char *text, size_t len);   //把一行内容加在末尾，失败返回-1
int load_file(const char *filename);  //读取文件，成功返回0，失败返回-1（errno已设置）
void draw_document(int max_y, int max_x, const char *filename);   //打印文件内容，以top_row为顶行
void draw_status(int max_y, int max_x, const char *filename);     //画状态栏
int text_width(int max_x);                    //折行宽度
int text_row(int max_y);                      //视觉行数，最后一行留给状态栏
int line_vrows(int line, int width);          //第line行占几个视觉行
int visual_row(int y, int x, int width);      //文本(y,x)落在第几视觉行
void locate(int vrow, int width, int *pty, int *ptx);   //第 vrow 个视觉行对应文本坐标
void keep_top_line(int old_width, int new_width);  //改大小时顶行不跳
void clamp_cursor(void);                      //光标在文本范围内
void scroll_to_cursor(int rows, int width);  //光标滚动屏幕
void move_cursor(int ch, int width);             //光标移动
void cursor_to_fold(int line, int x0, int want);  //把光标放到某一折的第 want 列
void handle_key(int ch, int width);              //按键处理，编辑或移动
int insert_char(int ch, int width);              //在光标处插入一个字符
int delete_char_before(int width);               //Backspace：删掉光标前面的字符
int delete_char_after(int width);                //Del：删掉光标处的字符
int insert_newline(void);                        //Enter：在光标处插入换行
int split_line(int line, int cut);               //把第 line 行从 cut 处断成两行
int join_line(int line);                         //把第 line 行接到上一行末尾


int main(int argc, char *argv[])      //加入命令行参数
{    
    if(argc < 2)              //输入查验
    {
      fprintf(stderr, "用法：%s <filename>\n", argv[0]);
      return 1;
    }
    
    if(load_file(argv[1]) != 0)       //读文件，读不到就在进入ncurses之前给出提示
    {
      fprintf(stderr, "错误：无法打开或读取文件 \"%s\" (%s)\n",
                argv[1], strerror(errno));
      return 1;
    }
    
    initial();              //初始化ncurses  
    editor(argv[1]);
    
    endwin();           //结束ncurses模式，恢复终端
    return 0;
}


/*----------初始代码------------------*/

/*初始化ncurses*/
int initial(void)                         
{                       
    initscr();            //ncurses模式
    raw(); 		  //关闭缓冲区，字符输入即可使用(直接接收控制字符，不被终端拦截)
    noecho();		  //关闭回显，即时显示
    keypad(stdscr,TRUE);  //允许特殊按键，如方向键
    return 0;
}

/*主要实现，按光标位置滚动，排版，显示，并等待下一个*/
int editor(const char *filename)   
{
    int max_y, max_x;
    int ch;
    int last_x = 0;
    
    while(1){   
        getmaxyx(stdscr, max_y, max_x); //获取屏幕大小
        
        if(last_x != 0 && max_x != last_x)
          keep_top_line(text_width(last_x), text_width(max_x));
        last_x = max_x;
        
        scroll_to_cursor(max_y, text_width(max_x));  //走出屏幕就滚动
        draw_document(max_y, max_x, filename);
        
        ch = getch();
        if(ch == 17 || ch == 3)   //17为Ctrl+Q的ASCII码，3为Ctrl+C的ASCII码（保险）
          break;
          
        handle_key(ch, text_width(max_x));                      //编辑或移动光标
    }//while结束
    
    return 0;
}

/*------------读取与展示文件--------------*/
    
/*把一行内容加在末尾，失败返回-1*/
int add_line(const char *text, size_t len)   
{
    char **temp = realloc(line_content, (size_t)(line_count + 1) * sizeof(char *));
    if (temp == NULL)   { errno = ENOMEM; return -1; }
    line_content = temp;    //二级指针扩容
    
    line_content[line_count] = malloc(len + 1);   //字符串\0,要加一
    if(line_content[line_count] == NULL)    { errno = ENOMEM; return -1; }
    
    memcpy(line_content[line_count], text, len);
    line_content[line_count][len] = '\0';
    line_count++;
    return 0;
}

/*读取文件，成功返回0，失败返回-1（errno已置位）*/
int load_file(const char *filename)     
{
    FILE *fp = fopen(filename, "rb"); //二进制只读打开文件，自己处理换行
    if(fp == NULL)                   //打开文件检查, errno由fopen设置
      return -1;
    
    size_t cap = 128, len = 0;      //当前行缓冲区
    char *buf = malloc(cap);
    if(buf == NULL) 
    {
      fclose(fp);
      errno = ENOMEM;
      return -1;
    }
    
    int ch;
    int failed = 0;
    while( (ch = fgetc(fp)) != EOF)          //解决换行与字符添加
    {
      if(ch == '\n')          //Unix 换行 \n
      {
        if(add_line(buf, len) != 0)   { failed = 1; break; }
        len = 0;
      }
      else if(ch == '\r')     //windows 换行 \r\n 或 老Mac \r
      {
        int next =fgetc(fp);
        if(next != '\n' && next != EOF)
          ungetc(next, fp);          //不是\r\n就把字符退回
        if(add_line(buf, len) != 0)   { failed = 1; break; }
        len = 0;
      }
      else                    //普通字符
      {
        if(len + 1 >= cap)
        {
          cap *= 2;
          char *nbuf = realloc(buf, cap);
          if(nbuf == NULL)    { failed = -1; errno = ENOMEM; break; }
          buf = nbuf;
        }
        buf[len++] = (char)ch;      //字符添加
      }
  
    } //while结束
    if(failed) { free(buf); fclose(fp); return -1; }    //写入成功审查
    
    if(len > 0)                  //文件最后一个字符不是换行时，最后一段写入
    {
      if(add_line(buf, len) != 0)   { free(buf); fclose(fp); return -1; }
    }
    
    free(buf);
    fclose(fp);   
    
    if(line_count == 0)       //空文件，创建一行无内容
    {
      if(add_line("", 0) != 0)   
        return -1;
    }
      
    return 0;
}

/*打印文件内容，以top_row为顶行*/
void draw_document(int max_y, int max_x, const char *filename)   
{
    int row_v = 0, x = 0, y = 0;
    int width = text_width(max_x);
    
    erase();
    locate(top_row, width, &y, &x);   //屏幕第一行对应文本位置
    
    for(row_v = 0; row_v < max_y; row_v++)
    {
      int len;
      
      if(y >= line_count)             //文本画完了，剩下的行留空
        break;
      
      len = (int)strlen(line_content[y]); //当前行剩余字符长度
      if(x < len)                     //这一行有字符
      {
        int n = len - x;
        if(n > width)
          n = width;
        mvaddnstr(row_v, 0, line_content[y] + x, n);  //写入剩余字符
      }
      
      x += width;     //下一折
      if(x > len)     //该行画完，换下一行
      {
        y++;
        x = 0;
      }
      //x == len 留给行尾光标
    } //for结束
    
    draw_status(max_y, max_x, filename);
    cursor(max_y, max_x);
    refresh();
}

/*最后一行状态栏：文件名 | 编译器状态 | 是否修改 | 光标行列
  屏幕只有一行时不画状态栏*/
void draw_status(int max_y, int max_x, const char *filename)
{
    char buf[100];
    int len = 0;
    int row = max_y - 1;        //最后一行画状态栏
    
    if(max_y < 2)
      return;                   //屏幕太小，不放状态栏
    
    len = snprintf(buf, sizeof(buf), "%s | %s",
                filename ? filename : "(无文件名)",  editor_state);   //n为前两个状态的字符长度
    if(modified && len > 0 && len < (int)sizeof(buf))       //有未保存的修改
      len += snprintf(buf + len, sizeof(buf) - (size_t)len, " | Modified");
    if(len > 0 && len < (int)sizeof(buf))                   //光标所在行列
      snprintf(buf + len, sizeof(buf) - (size_t)len, " | %d:%d", 
                cur_y + 1, cur_x + 1);      //暂时设为展示文本行列，而非屏幕行列（后有需求可更改）
    
    attron(A_REVERSE);            //反色显示，和正文区分开
    mvaddnstr(row, 0, buf, max_x - 1);    //太长会被截掉
    attroff(A_REVERSE);
}

/*-----------折行：文本行与视觉行-------------*/

/*折行宽度，最后一列不写，避免自动换行*/
int text_width(int max_x)
{
    return max_x > 1 ? max_x - 1 : 1;
}

/*视觉行数，最后一行留给状态栏*/
int text_row(int max_y)                      
{
    return max_y > 1 ? max_y - 1 : 1;   //屏幕只有一行时，不画状态栏
}

/*第 line 行折行后占几个视觉行*/
int line_vrows(int line, int width)
{
    return (int)strlen(line_content[line]) / width + 1;
}

/*文本(y,x)落在第几视觉行*/
int visual_row(int y, int x, int width)
{
    int i = 0, n=0;
    for(i = 0; i < y; i++)          //前y行
      n += line_vrows(i, width);
    return n + x / width;
}

/*第 vrow 个视觉行对应文本里第几行，第几个字符*/
void locate(int vrow, int width, int *pty, int *ptx)
{
    int i = 0;
    while(i < line_count && vrow >= line_vrows(i, width))   //除去前 vrow-1 个视觉行
    {
      vrow -= line_vrows(i, width);
      i++;
    }
    
    *pty = i;                    //文本末尾时，i==line_count
    *ptx = vrow * width;         //每一折占 width 个字符
}

/*终端宽度改变时，原来处于屏幕顶行的文本依旧在顶行*/
void keep_top_line(int old_width, int new_width)
{
    int ty, tx;
    locate(top_row, old_width, &ty, &tx);       //原来顶行对应文本位置
    top_row = visual_row(ty, tx, new_width);    //更新视觉行号
}

/*-------光标------------*/

/*光标定位*/
int cursor(int max_y,int max_x)       
{
    int width = text_width(max_x);
    int row = visual_row(cur_y, cur_x, width) - top_row;
    int col = cur_x % width;
    
    if(row < 0)
      row = 0;
    if(row > max_y - 1)
      row = max_y -1;
      
    move(row, col);
    
    return 0;
}

/*光标在文本范围内*/
void clamp_cursor(void)
{
    int len;  
    
    if(cur_y < 0) cur_y = 0;
    if(cur_y > line_count - 1) 
      cur_y = line_count - 1;
    
    len = (int)strlen(line_content[cur_y]);
    if(cur_x < 0)
      cur_x = 0;
    if(cur_x > len)
      cur_x = len;
    
}

/*光标滚动屏幕*/
void scroll_to_cursor(int rows, int width)
{
    int cur_vy = visual_row(cur_y, cur_x, width);   //光标所在视觉行
    int totalvr = 0;                                //视觉行总数
    int i;
    
    for(i =0 ; i<line_count; i++)                   //文档总共视觉行
      totalvr += line_vrows(i, width);
    
    if(cur_vy < top_row)                               //光标往屏幕上方滚动
      top_row = cur_vy;
    else if(cur_vy >= top_row + rows)                  //光标往屏幕下方滚动
      top_row = cur_vy - rows + 1;
    
    /*窗口在屏幕内*/
    if(top_row > totalvr - rows)
      top_row = totalvr - rows;
    if(top_row < 0)
      top_row = 0;
}

/*光标移动*/
void move_cursor(int ch, int width)            
{
    int len = (int)strlen(line_content[cur_y]);
    int fold = cur_x / width;     //光标在文本行第几折
    int want = want_x;            //想去的屏幕列
    
    if(want > width - 1)          //窗口变窄后改变
      want = width - 1;

    switch (ch) {
    case KEY_UP:                      //上一行
        if (fold > 0)                 //同一条文本行的上一折
          cursor_to_fold(cur_y, (fold - 1) * width, want);
        else if(cur_y > 0)            //上一文本行的最后一折
        {
          int prev_len = (int)strlen(line_content[cur_y - 1]);
          cursor_to_fold(cur_y - 1, prev_len / width * width, want);
        }
        break;

    case KEY_DOWN:                    //下一行
        if ((fold + 1) * width <= len)  //同一条文本行下一折
          cursor_to_fold(cur_y, (fold + 1) * width, want);
        else if(cur_y < line_count - 1) //下一行第一折
          cursor_to_fold(cur_y + 1, 0, want);
        break;

    case KEY_LEFT:                    //左移；行首再往左就到上一行的行尾
        if (cur_x > 0)
            cur_x--;
        else if (cur_y > 0) {
            cur_y--;
            cur_x = (int)strlen(line_content[cur_y]);
        }
        break;

    case KEY_RIGHT:                   //右移；行尾再往右就到下一行的行首
        if (cur_x < len)
            cur_x++;
        else if (cur_y < line_count - 1) {
            cur_y++;
            cur_x = 0;
        }
        break;

    default:                          //这一阶段只处理方向键
        break;
    }

    clamp_cursor();
    
    if(ch == KEY_LEFT || ch == KEY_RIGHT)     //左右移动更新 want_x
      want_x = cur_x % width;
}

/*光标放在第 line 行，从 x0 开始的那一行里，屏幕第 want 列的位置
  x0 必须是一折的起点， want 是想去的这内序号*/
void cursor_to_fold(int line, int x0, int want)
{
    int len = (int)strlen(line_content[line]);
    int rest = len - x0;      //该文本行剩余字符
    
    if(rest < 0)
      rest = 0;
    
    cur_y = line;
    cur_x = x0 + (want < rest ? want : rest);
}

/*-------------编辑文本---------------*/

/*把line行接到line-1行末尾，并删掉line行
  成功返回0, 失败返回1（errno已置位）*/
int join_line(int line)
{
    int prev_len = (int)strlen(line_content[line - 1]);
    int len = (int)strlen(line_content[line]);
    char *new_prev = realloc(line_content[line - 1], (size_t)(prev_len + len + 1));
    //给前一个文本行分配新空间
    if(new_prev == NULL)    { errno = ENOMEM; return -1; }
    line_content[line - 1] = new_prev;
    
    memcpy(line_content[line - 1] + prev_len, line_content[line], (size_t)len + 1);
    free(line_content[line]);           //释放无用内存
    memmove(line_content + line, line_content + line + 1,
            (size_t)(line_count - line - 1) * sizeof(char*));   //之后的文本行全部前移
    line_count--;
    
    return 0;
}

/*把line行从第 cut 个字符处断开，后半段成为新的line+1行
  成功返回0,失败返回-1（errno已置位）*/
int split_line(int line, int cut)
{
    int len = (int)strlen(line_content[line]);
    char *tail = malloc((size_t)(len - cut) + 1); //后半段，\0占一个
    char **temp = NULL;
    
    if(tail == NULL)     { errno = ENOMEM; return -1; }   //后半段
    memcpy(tail, line_content[line] + cut, (size_t)(len - cut));
    tail[len - cut] = '\0';
    
    temp = realloc(line_content, (size_t)(line_count + 1) * sizeof(char*));   
    if(temp == NULL)
    { free(tail); errno = ENOMEM; return -1;}
    line_content = temp;                          //文本总行数扩大
    
    memmove(line_content + line + 2, line_content + line + 1,
            (size_t)(line_count - line - 1) * sizeof(char*));   //后面的行后移
    line_content[line + 1] = tail;
    line_content[line][cut] = '\0';             //原行从cut处切断
    line_count++;
    
    return 0;
}

/*在光标处插入一个字符，成功返回0，失败返回-1,（errno已置位）*/
int insert_char(int ch, int width)
{
    int len = (int)strlen(line_content[cur_y]);
    char *new_line = realloc(line_content[cur_y], (size_t)len + 2);   //扩大容量，\0与新字符
    
    if(new_line == NULL)    { errno = ENOMEM; return -1; }
    line_content[cur_y] = new_line;
    
    memmove(line_content[cur_y] + cur_x + 1, line_content[cur_y] + cur_x, 
            (size_t)(len - cur_x + 1));       //把光标后面字符与\0整体后移一格
    line_content[cur_y][cur_x] = (char)ch;    //写入用户输入的字符
    
    cur_x++;                                  //光标停在输入的字符后
    want_x = cur_x % width;
    modified = 1;                             //文本改过了
    return 0;
}

/*Backspace 删掉光标前面的字符
    若光标在行首，则删掉上一行末尾的换行，再拼接起来*/
int delete_char_before(int width)
{
    int len = (int)strlen(line_content[cur_y]);
    
    if(cur_x > 0)             //本行内，删掉前一个字符
    {
      memmove(line_content[cur_y] + cur_x - 1, line_content[cur_y] + cur_x, 
              (size_t)(len - cur_x + 1));
      cur_x--;
      want_x = cur_x % width;
      modified = 1;                             //文本改过了
    }
    else if(cur_y > 0)        //行首，和上一行合并
    {
      int prev_len = (int)strlen(line_content[cur_y - 1]);
      if(join_line(cur_y) != 0)
        return -1;
      cur_y--;
      cur_x = prev_len;       //光标停在接缝处
      want_x = cur_x % width;
      modified = 1;                             //文本改过了
    }
    //文件开头再按就无反应
    
    return 0;
}

/*Del 删掉光标处的字符
  若光标在行尾，删掉行尾换行，接上下一行*/
int delete_char_after(int width)
{
    int len = (int)strlen(line_content[cur_y]);
    
    if(cur_x < len)     //本行内，删掉光标处字符
    {
      memmove(line_content[cur_y] + cur_x, line_content[cur_y] + cur_x + 1,
              (size_t)(len - cur_x));
      modified = 1;                             //文本改过了
    }
    else if(cur_y < line_count - 1)   //行尾，和下一行合并
    {
      if(join_line(cur_y + 1) != 0)
        return -1;
      modified = 1;                             //文本改过了
    }
    //文件末尾再按无效果
    
    want_x = cur_x % width;
    return 0;
}

/*Enter 在光标处插入换行*/
int insert_newline(void)
{
    if(split_line(cur_y, cur_x) != 0)
      return -1;
      
    cur_y++;                //光标到新行
    cur_x = 0;
    want_x = 0;
    modified = 1;                             //文本改过了
    return 0;
}

/*输入按键处理，编辑或移动光标
  内存不足等问题在2.3中解决*/
void handle_key(int ch, int width)
{
    if(ch == KEY_BACKSPACE || ch == 127 || ch == 8)         //Backspace(有的终端发127,有的发8)
      delete_char_before(width);
    else if(ch == KEY_DC)             //Del
      delete_char_after(width);
    else if(ch == KEY_ENTER || ch == '\n' || ch == '\r')    //Enter
      insert_newline();
    else if(ch >=32 && ch < 256 && ch != 127)     //可打印字符
      insert_char(ch, width);
    else                              //方向键
      move_cursor(ch, width);
}
