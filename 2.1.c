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


/*----------函数声明----------------*/
int initial(void);                        //初始化ncurses
int cursor(int max_y,int max_x);        //光标定位
int editor(void);                     //主要实现，按光标位置滚动，排版，显示，并等待下一个
int add_line(const char *text, size_t len);   //把一行内容加在末尾，失败返回-1
int load_file(const char *filename);  //读取文件，成功返回0，失败返回-1（errno已设置）
void draw_document(int max_y, int max_x);     //打印文件内容，以top_row为顶行
int text_width(int max_x);                    //折行宽度
int line_vrows(int line, int width);          //第line行占几个视觉行
int visual_row(int y, int x, int width);      //文本(y,x)落在第几视觉行
void locate(int vrow, int width, int *pty, int *ptx);   //第 vrow 个视觉行对应文本坐标
void keep_top_line(int old_width, int new_width);  //改大小时顶行不跳
void clamp_cursor(void);                      //光标在文本范围内
void scroll_to_cursor(int rows, int width);  //光标滚动屏幕
void move_cursor(int ch, int width);             //光标移动
void cursor_to_fold(int line, int x0, int want);  //把光标放到某一折的第 want 列


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
    editor();
    
    endwin();           //结束ncurses模式，恢复终端
    return 0;
}



int initial(void)                         //初始化ncurses
{                       
    initscr();            //ncurses模式
    raw(); 		  //关闭缓冲区，字符输入即可使用(直接接收控制字符，不被终端拦截)
    noecho();		  //关闭回显，即时显示
    keypad(stdscr,TRUE);  //允许特殊按键，如方向键
    return 0;
}

int cursor(int max_y,int max_x)       //光标定位
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

int editor(void)   //主要实现，按光标位置滚动，排版，显示，并等待下一个
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
        draw_document(max_y, max_x);
        
        ch = getch();
        if(ch == 17 || ch == 3)   //17为Ctrl+Q的ASCII码，3为Ctrl+C的ASCII码（保险）
          break;
          
        move_cursor(ch, text_width(max_x));                      //重新定位光标
    }//while结束
    
    return 0;
}

    
int add_line(const char *text, size_t len)   //把一行内容加在末尾，失败返回-1
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

int load_file(const char *filename)     //读取文件，成功返回0，失败返回-1（errno已设置）
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

void draw_document(int max_y, int max_x)     //打印文件内容，以top_row为顶行
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
    
    cursor(max_y, max_x);
    refresh();
}


/*-----------折行：文本行与视觉行-------------*/

/*折行宽度，最后一列不写，避免自动换行*/
int text_width(int max_x)
{
    return max_x > 1 ? max_x - 1 : 1;
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

