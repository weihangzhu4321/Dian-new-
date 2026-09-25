#include <ncurses.h>
#include <stdio.h>        
#include <stdlib.h>       
#include <string.h>
#include <errno.h>

static char **line_content = NULL;    //行内容
static int    line_count   = 0;       //行数

int initial();                        //初始化ncurses
int cursor(int y,int x);              //光标定位
int simpleprint(int ch,int y,int x);  //简单绘制
int add_line(const char *text, size_t len);   //把一行内容加在末尾，失败返回-1
int load_file(const char *filename);  //读取文件，成功返回0，失败返回-1（errno已设置）
void draw_document(int max_y, int max_x);     //打印文件内容


int main(int argc, char *argv[])      //加入命令行参数
{       
    int ch=EOF;
    int x = 0, y = 0;        //x为列，y为行
    int max_x, max_y;
    
    if(argc < 2)              //输入查验
    {
      fprintf(stderr, "用法：%s <filename>\n", argv[0]);
      return 1;
    }
    
    if(load_file(argv[1]) != 0)       //读文件，读不到就在进入ncurses之前给出提示
    {
      fprintf(stderr, "错误：无法打开或读取文件 \"%s\" (%s)\n", argv[1], strerror(errno));
      return 1;
    }
    
    initial();              //初始化ncurrses  
    getmaxyx(stdscr, max_y, max_x);     //获取屏幕大小
    
    draw_document(max_y, max_x);      // 打印文件内容
    
    cursor(y, x);           //初始为(0,0)     
    simpleprint(ch, y, x);  //简单绘制
    
    endwin();           //结束ncurses模式，恢复终端
    return 0;
}



int initial()                         //初始化ncurses
{                       
    initscr();            //ncurses模式
    raw(); 		  //关闭缓冲区，字符输入即可使用(raw直接接收控制字符，不被终端拦截)
    noecho();		  //关闭回显，即时显示
    keypad(stdscr,TRUE);  //允许特殊按键，如方向键
    return 0;
}

int cursor(int y,int x)       //光标定位
{
    move(y,x);            //(行，列）对应(y,x)
    refresh();            //显示
    return 0;
}

int simpleprint(int ch,int y,int x)   //简单绘制
{
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x); //获取初始屏幕大小
    
    while(1){   
    
        ch = getch();
        if (ch == 17 || ch == 3)       //17为(crtl+Q)的ASCII码,3为(ctrl+C)的ASCII码（保险）
            break;
        
        getmaxyx(stdscr, max_y, max_x);           //重新获得大小
        
        //定位
        if(ch == KEY_UP) 
        { if(y > 0)         y--;}      //y上移
        else if(ch == KEY_DOWN) 
        { if(y < max_y - 1) y++;}      //y下移
        else if(ch == KEY_LEFT) 
        { if(x > 0)         x--;}      //x左移
        else if(ch == KEY_RIGHT)
        { if(x < max_x - 2)  x++;}      //x右移
        
        //打印
        else if(ch >= 32 && ch <= 126){           //ASCII码 32到126包含所有可打印字符
            addch(ch);                            //在光标处画出字符
            if (x < max_x - 2) x++;                //打印防越界
        }
        
        cursor(y, x);                              //重新定位光标
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

int load_file(const char *filename)         //读取文件，成功返回0，失败返回-1（errno已设置）
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
          if(nbuf == NULL)    { errno = ENOMEM; break; }
          buf = nbuf;
        }
        buf[len++] = (char)ch;      //字符添加
      }
  
    } //while结束
    if(failed) { free(buf); fclose(fp); return -1; }    //写入成功审查
    
    if(len > 0)                  //文件最后一个字符不是换行时，最后一段写入
      add_line(buf, len);
    
    free(buf);
    fclose(fp);   
    
    if(line_count == 0)       //空文件，创建一行无内容
      add_line("", 0);
      
    return 0;
}

void draw_document(int max_y, int max_x)     //打印文件内容
{
    int row;
    int screen_y = 0; // 记录当前画到了屏幕的第几行，以便超出时换行

    erase();

    // 遍历文件里的每一行
    for (row = 0; row < line_count; row++)        // 遍历文件里的每一行
    {
        int len = (int)strlen(line_content[row]); // 当前行有多少个字符
        int i = 0;                         // 当前行处理到第几个字符了

        if (len == 0)             // 如果这一行是空行，也要在屏幕上占一行
        {
            screen_y++;
            if (screen_y >= max_y) 
              break; // 屏幕画满了，退出
            continue;
        }

        while (i < len)         // 把这一行的字符，一个一个画到屏幕上
        {
            // 计算这一截能塞下多少个字符
            int chars_left = len - i;       // 还剩多少个字符
            int space_left = max_x - 1;     // 当前屏幕行还能放几个字符（留一列防自动换行）
            int chunk = (chars_left < space_left) ? chars_left : space_left; // 取较小值

            // 如果屏幕还有位置，才把这截字符画上去
            if (screen_y < max_y) 
              mvaddnstr(screen_y, 0, line_content[row] + i, chunk);
      
            i += chunk;       // 这一行处理了这么多字符
            screen_y++;       // 移动到屏幕的下一行

            // 如果屏幕已经被画满了，后面的内容就画不下了，直接结束
            if (screen_y >= max_y) 
              break;
        }   //while结束

        // 如果屏幕已经满了，跳出外层循环，不再处理后面的文件行
        if (screen_y >= max_y) 
            break;
    }   //for结束

    refresh();
}
