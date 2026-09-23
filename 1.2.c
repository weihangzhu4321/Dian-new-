#include <ncurses.h>

int initial();                        //初始化ncurrses
int cursor(int x,int y);              //光标定位
int simpleprint(int ch,int x,int y);  //主要实现


int main(){
    int ch=EOF;
    int x=0,y=0;        //x为列，y为行
    
    initial();            
    cursor(x,y);        //初始为(0,0)     
    simpleprint(ch, x, y);
    
    endwin();           //结束ncurses模式
    
    return 0;
}



int initial()                         //初始化ncurses
{                       
    initscr();            //ncurses模式
    cbreak(); 		  //关闭缓冲区
    noecho();		  //关闭回显
    keypad(stdscr,TRUE);  //允许特殊按键
    return 0;
}

int cursor(int x,int y)       //光标定位
{
    move(y,x);            //(行，列）对应(y,x)
    refresh();            //显示
    return 0;
}

int simpleprint(int ch,int x,int y)   //主要实现
{
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x); //获取初始屏幕大小
    while( (ch = getch()) != 27){   //27为Esc的ASCII码
    
        //定位
        if(ch == KEY_UP) 
        { if(y > 0)         y--;}      //y上移
        else if(ch == KEY_DOWN) 
        { if(y < LINES - 1) y++;}      //y下移
        else if(ch == KEY_LEFT) 
        { if(x > 0)         x--;}      //x左移
        else if(ch == KEY_RIGHT)
        { if(x < COLS - 1)  x++;}      //x右移
        
        //打印
        else if(ch >= 32 && ch <= 126){           //ASCII码 32到126包含所有可打印字符
            addch(ch);                            //在光标处画出字符
            if (x < COLS - 1) x++;                //打印防越界
        }
        
        getmaxyx(stdscr, max_y, max_x);           //重新获得大小
        
        cursor(x,y);                              //重新定位光标
    }//while结束
    
    return 0;
}
    
