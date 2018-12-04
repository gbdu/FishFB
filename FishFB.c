#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <ctype.h>
#include <shellapi.h>

#define MAX_SEARCH_INPUT 20 // max input for search
#define MAX_HISTORY 50
#define LWA_ALPHA 2
#define MAX_COPY 10

typedef BOOL (WINAPI* SLWA)(HWND, COLORREF, BYTE, DWORD);

// Functions
LRESULT CALLBACK window_process (HWND, UINT, WPARAM, LPARAM); // Window process

void error(char * from); // Give API error then quit
void get_parent_dir(char dirc[MAX_PATH], char * parent); // Parse string to get parent dir
void strlower(char * str); // lower case a string

void notify(char * message); // Notify user via a message box
void notifyd(int d); // Notify user via a message box
void notifyc(char c); 

int get_file_count(char dir[MAX_PATH]);
float get_file_size(char path[MAX_PATH]);

BOOL file_exists(char dir[MAX_PATH]); // returns 1 if file exits

// threads
DWORD WINAPI t_dir_change(PVOID param); // this sends a message to window_process if a change happens in a dir
DWORD WINAPI CopyProgressRoutine(LARGE_INTEGER total_file_size, LARGE_INTEGER total_transferred,
				 LARGE_INTEGER stream, LARGE_INTEGER stream_transferred, DWORD current_stream, DWORD reason, 
				 HANDLE source, HANDLE dest, LPVOID data);
	
// thread handles
HANDLE ht_dir_change;
HANDLE ht_copy_proc;

// variables
int selected = 0; // current file user has selected 
int files_start = 0; // ID of the file to start with
int files_per_area = 0; // How many files can be listed at once
int files_before = 0;
int file_list = 0; // files currently listed (limit for the selected++/--
int history_id = 0; // number of currently archived directories
int ongoing_copy = 0;
TEXTMETRIC tm;

// flags
BOOL f_selected_dir = 0; // is selected file a directory?
BOOL f_show_search = 0; // show search bar
BOOL f_ignore_char = 0; // tells the WM_CHAR clause to ignore the F character 
BOOL f_show_addressbar = 0;
BOOL f_dir_change_executing = 0; // is t_dir_change executing?
BOOL f_copy_selected = 0; // copy selected file

// strings 
char selected_path[MAX_PATH]; // path to current file (updated in the WM_PAINT clause)
char selected_dir[MAX_PATH]; // path to current dir
char selected_file[FILENAME_MAX]; // selected filename
char search_input[MAX_SEARCH_INPUT]; // filename input for search
char dir_input[MAX_PATH+2]; 
char copy_filename[FILENAME_MAX];
char copy_path[FILENAME_MAX+1];

char * app_name = "FishFB"; // For caption bar and whatnot
HWND hwnd; // main window handle

struct { // user setting
    int pos_x; // x cordinates of window
    int pos_y; // y cordinates of window
    int width; // width of window
    int height; // height of window
    int transparency; // window transparency
    int font_size; // size of font
    char font[33]; // font name

    COLORREF chidden; // color of hidden files
    COLORREF cselected; // color of selected file
    COLORREF cnormal; // color of normal files listed
    COLORREF cborders; // color of borderss
    COLORREF cselected_dir; // color of directory at top
    COLORREF cdir; // color of dir listed
    COLORREF cbackground; // background of application
    COLORREF carrows; // color of scroll bars arrows
    COLORREF cfooter; // color of info at the bottom
    COLORREF caddress_bar; // bg of the address bar when in use
    COLORREF ccopy_into_dir; // color of dir item when signaling to insert current copied file into it
    COLORREF ccopy_in_progress; 

    int border_style; // style (see CreatePen)

    BOOL f_ignore_case; // ignore character case in search?
} user_config;

struct {
    char path[MAX_PATH];
} history[MAX_HISTORY];

struct {
    char path[MAX_PATH];
    int done;
    HANDLE file; // handle to file for the progress bar for the callback
    double kb_copied;
    double kb_full; // full size of file
} copy_progress[MAX_COPY];

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev_instance,
		   PSTR cmd_line, int cmd_show){
    MSG window_msg;
    WNDCLASS mainwnd;

    user_config.pos_x = 80;
    user_config.pos_y = 200;
    user_config.width = 400;
    user_config.height = 500;
    user_config.transparency = 200;
    user_config.font_size = 16;
	
    user_config.chidden = RGB(170, 170, 170);
    user_config.cselected = RGB(255, 255, 255);
    user_config.cnormal = RGB(0, 255, 0);
    user_config.cborders = RGB(00, 140, 160);
    user_config.cselected_dir = RGB(255, 100, 100);
    user_config.cdir = RGB(255, 255, 0);
    user_config.cbackground = RGB(0, 0, 0);
    user_config.carrows = RGB(255, 255, 0);
    user_config.cfooter = RGB(255, 100, 100);
    user_config.caddress_bar = RGB(0, 10, 60);
    user_config.ccopy_into_dir = RGB(255, 200, 200);
    user_config.ccopy_in_progress = RGB(0, 0, 255);

    user_config.border_style = PS_SOLID;
    user_config.f_ignore_case = 1;

    sprintf(user_config.font, "fixed");

    memset(&mainwnd, 0, sizeof(WNDCLASS));
    mainwnd.style = CS_HREDRAW | CS_VREDRAW;
    mainwnd.lpfnWndProc = &window_process;
    mainwnd.cbClsExtra = 0;
    mainwnd.cbWndExtra = 0;
    mainwnd.hInstance = instance;
    mainwnd.hIcon = LoadIcon(instance, "ICON_OPEN");
    mainwnd.hCursor = LoadCursor (NULL, IDC_ARROW); // default IDC_ARROW
    mainwnd.hbrBackground = CreateSolidBrush(user_config.cbackground);
    mainwnd.lpszClassName = app_name;
    mainwnd.lpszMenuName  = NULL; 
	
    if(!RegisterClass(&mainwnd)){
        error("RegisterClass");
    }
	
    if(!AddFontResource("fixed.fon")){
	error("from:AddFontResource");
    }
	
    HWND hdesk = GetDesktopWindow();
    hwnd = CreateWindow(app_name, app_name, WS_POPUP | WS_MAXIMIZE	, 
			user_config.pos_x, user_config.pos_y, user_config.width,
			user_config.height, hdesk, NULL, instance, NULL);
    if(!hwnd){
        error("from:CreateWindow");
    }
    SLWA SetLayeredWindowAttributes = 0;
    HMODULE hDLL = GetModuleHandle("USER32.DLL");
    SetLayeredWindowAttributes = 
	(SLWA)GetProcAddress(hDLL, "SetLayeredWindowAttributes");
    if(SetLayeredWindowAttributes == 0){
	error("from:SetLayeredWindowAttributes");
    }
    DWORD dwStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    dwStyle = dwStyle | WS_EX_LAYERED;
    SetWindowLong(hwnd, GWL_EXSTYLE, dwStyle);
    SetLayeredWindowAttributes(hwnd, 0, user_config.transparency, LWA_ALPHA);
    FreeLibrary(hDLL);

    HWND hprogman = FindWindow("Progman","Program Manager");
    if (hprogman == NULL){
        hprogman = FindWindow("#32769","");
    }
    SetParent(hwnd, hprogman);
    SetWindowLong(hwnd, GWL_STYLE,(GetWindowLong(hwnd, GWL_STYLE) | WS_CHILD));	
    ShowWindow (hwnd, cmd_show);
    UpdateWindow(hwnd);

    while(GetMessage (&window_msg, NULL, 0, 0)){
        TranslateMessage(&window_msg);
        DispatchMessage(&window_msg);
    }

    return window_msg.wParam;
}
LRESULT CALLBACK window_process (HWND hwnd, UINT message, WPARAM
				 wParam, LPARAM lParam){
    HDC hdc;
    PAINTSTRUCT paint_class;

    if(message == WM_CREATE){
	sprintf(selected_dir, "H:\\Documents and Settings\\Gargantua\\Desktop\\");
	strcpy(dir_input, selected_dir);
    }
    else if(message == WM_PAINT) {
	hdc = BeginPaint(hwnd, &paint_class);
	SelectObject(hdc, CreateFont(user_config.font_size, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				     user_config.font));
	GetTextMetrics(hdc, &tm);

	int counter = 0;
	int y = tm.tmHeight + tm.tmExternalLeading + 5;
	char dir_astrick[MAX_PATH+1];
	sprintf(dir_astrick, "%s*", selected_dir);
		
	int files = get_file_count(dir_astrick); 
	if(f_dir_change_executing == 0){
	    DWORD threadid;
	    ht_dir_change = CreateThread(NULL, 0, t_dir_change, 0, 0, &threadid);
	}
	files_before = files;
	WIN32_FIND_DATA file_count;

	SetTextColor(hdc, user_config.cselected_dir);
	SetBkMode(hdc, TRANSPARENT);
	TextOut(hdc, 3, 3, selected_dir, strlen(selected_dir));
	SelectObject(hdc, CreatePen(user_config.border_style, 1, user_config.cborders));
	if(f_show_search == 1){
	    y += tm.tmHeight + tm.tmExternalLeading + 3 ;
	    MoveToEx (hdc, 0, y - (tm.tmHeight + tm.tmExternalLeading + 3), NULL);
	    LineTo (hdc, user_config.width, y - (tm.tmHeight + tm.tmExternalLeading + 3));

	    char * temp = (char *) malloc(9 + strlen(search_input));
	    if(temp == NULL){
		error("Not enough memory.");
	    }
	    if(search_input != NULL){
		sprintf(temp, "Search: %s", search_input);
	    }
	    else{
		sprintf(temp, "Search: ");
	    }

	    files_per_area = (user_config.height - (tm.tmHeight + tm.tmExternalLeading + 180) )
		/ (tm.tmHeight + tm.tmExternalLeading);
			
	    TextOut(hdc, 3, y - (tm.tmHeight + tm.tmExternalLeading - 1), temp, strlen(temp));
	    free(temp);
	}
	else if(f_show_addressbar == 1){
	    SelectObject(hdc, CreateSolidBrush(user_config.caddress_bar));
	    SelectObject(hdc, CreatePen(0, 1, 0));

	    Rectangle(hdc, 0, 0, user_config.width, (tm.tmHeight + tm.tmExternalLeading + 4));
	    TextOut(hdc, 3, 3, dir_input, strlen(dir_input));
	}

	files_per_area = (user_config.height - (tm.tmHeight + tm.tmExternalLeading + 60))
	    /(tm.tmHeight + tm.tmExternalLeading);

	SelectObject(hdc, CreatePen(user_config.border_style, 1, user_config.cborders));
		
	MoveToEx (hdc, 0, y, NULL);
	LineTo (hdc, user_config.width, y) ; // Top line
		
	MoveToEx (hdc, 16, y, NULL);
	LineTo (hdc, 16, user_config.height - 60) ; // Side line

	WIN32_FIND_DATA * files_wfd = (WIN32_FIND_DATA *) malloc(files * sizeof(WIN32_FIND_DATA));
	if(files_wfd == NULL){
	    error("Not enough memory.");
	}
	HANDLE fh = FindFirstFile(dir_astrick, &files_wfd[0]);
	if(fh == INVALID_HANDLE_VALUE){
	    error("from:FindFirstFile");
	}

	counter = 0;
	WIN32_FIND_DATA temp_wfd;
	while(FindNextFile(fh, &temp_wfd)){
	    if(search_input != NULL && f_show_search == 1){
		if(user_config.f_ignore_case == 1){
		    char * low_filename = (char *) malloc(strlen(temp_wfd.cFileName) + 1);
		    if(low_filename == NULL){
			error("Not enough memory.");
		    }
		    char * low_input = (char *) malloc(strlen(search_input) + 1);
		    if(low_input == NULL){
			error("Not enough memory.");
		    }
		    strcpy(low_filename, temp_wfd.cFileName);
		    strcpy(low_input, search_input);
					
		    strlower(low_input);
		    strlower(low_filename);

		    if(strstr(low_filename, low_input) ) {
			memcpy(&files_wfd[counter], &temp_wfd, sizeof(WIN32_FIND_DATA));
			counter++;
		    }
		    free(low_input);
		    free(low_filename);
		}
		else if(strstr(temp_wfd.cFileName, search_input) ) {
		    memcpy(&files_wfd[counter], &temp_wfd, sizeof(WIN32_FIND_DATA));
		    counter++;
		}
	    }
	    else{
		memcpy(&files_wfd[counter], &temp_wfd, sizeof(WIN32_FIND_DATA));
		counter++;
	    }
	}
	FindClose(fh);
		
	char * dir_name = (char *) malloc(FILENAME_MAX + strlen(copy_filename) + 30);
	if(dir_name == NULL){
	    error("Not enough memory");
	}
	file_list = counter;
	y += 5;

	counter = 0;
	while((counter != file_list) && (y < user_config.height - 
					 (60 + tm.tmHeight + tm.tmExternalLeading))){
	    if(counter >= files_start){
		if(files_wfd[counter].dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){
		    if(files_wfd[counter].dwFileAttributes & FILE_ATTRIBUTE_HIDDEN){
			SetTextColor(hdc, user_config.chidden);
		    }
		    if(counter == selected){
			SetTextColor(hdc, user_config.cselected);
			if(f_copy_selected && strcmp(files_wfd[counter].cFileName, copy_filename)){
			    sprintf(dir_name, "%s\\ <- %s", files_wfd[counter].cFileName, copy_filename);
			}
			else{
			    sprintf(dir_name, "%s\\", files_wfd[counter].cFileName);
			}
		    }
		    else{
			SetTextColor(hdc, user_config.cdir);
			sprintf(dir_name, "%s\\", files_wfd[counter].cFileName);
		    }
					
		    if(counter == selected){
			sprintf(selected_path, "%s%s", selected_dir, 
				files_wfd[counter].cFileName);
			f_selected_dir = 1;
			strcpy(selected_file, files_wfd[counter].cFileName);
		    }
		    TextOut(hdc, 25, y, dir_name, strlen(dir_name));
		}
		else if(counter == selected){
		    SetTextColor(hdc, user_config.cselected);
		    TextOut(hdc, 25, y, files_wfd[counter].cFileName, 
			    strlen(files_wfd[counter].cFileName));
		    sprintf(selected_path, "%s%s", selected_dir, files_wfd[counter].cFileName);
		    strcpy(selected_file, files_wfd[counter].cFileName);
		    f_selected_dir = 0;	
		}
		else{
		    if(files_wfd[counter].dwFileAttributes & FILE_ATTRIBUTE_HIDDEN){
			SetTextColor(hdc, user_config.chidden);
		    }
		    else if(ongoing_copy != 0){
			int t = 0;
			while(t != ongoing_copy){
			    sprintf(dir_name, "%s%s", selected_dir, files_wfd[counter].cFileName);
			    if(!strcmp(copy_progress[t++].path, dir_name)){
				sprintf(dir_name, "%s (%%%d - %lf of %lf kbs)", files_wfd[counter].cFileName,
					copy_progress[t].done, copy_progress[t].kb_copied, copy_progress[t].kb_full);
				SetTextColor(hdc, user_config.ccopy_in_progress);
				TextOut(hdc, 25, y,	dir_name, strlen(dir_name));
			    }
			}
		    }
		    SetTextColor(hdc, user_config.cnormal);
		    TextOut(hdc, 25, y,	files_wfd[counter].cFileName, 
			    strlen(files_wfd[counter].cFileName));
		}
		y += tm.tmHeight + tm.tmExternalLeading;
	    }
	    counter++;
	}
	free(dir_name);
	// Arrows
	
	if((files_start + files_per_area) <= file_list-1){
	    SelectObject(hdc, CreatePen(PS_SOLID, 1, user_config.carrows));
			
	    y = user_config.height - 75;

	    MoveToEx(hdc, 4, y, NULL);
	    LineTo(hdc, 12, y) ;
	    LineTo(hdc, 8, y + 7);
	    LineTo(hdc, 4, y) ;
	}
	if(files_start != 0){
	    SelectObject(hdc, CreatePen(PS_SOLID, 1, user_config.carrows));
	    int ty = y;
	    if(f_show_search == 1){
		ty = (tm.tmHeight + tm.tmExternalLeading) * 3 + 10;
	    }
	    else{
		ty = (tm.tmHeight + tm.tmExternalLeading) * 2 + 10;
	    }
	    MoveToEx(hdc, 4, ty, NULL);
	    LineTo(hdc, 12, ty) ;
	    LineTo(hdc, 8, ty - 7);
	    LineTo(hdc, 4, ty) ;
	}

	//footer (the area that displays file info at the bottom)

	int footer_y = user_config.height - 50;

	SelectObject(hdc, CreatePen(user_config.border_style, 1, user_config.cborders));
	MoveToEx (hdc, 0, user_config.height - 60, NULL);
	LineTo (hdc, user_config.width, user_config.height - 60) ;
	SetTextColor(hdc, user_config.cfooter);
		
	if(files_wfd[selected].dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){
	    TextOut(hdc, 6, footer_y, "Directory: ", 11);
	    TextOut(hdc, 12 * tm.tmAveCharWidth, footer_y, 
		    files_wfd[selected].cFileName, 
		    strlen(files_wfd[selected].cFileName));
	}
	else{
	    TextOut(hdc, 6, footer_y, "File: ", 6);
	    TextOut(hdc, 7 * tm.tmAveCharWidth, footer_y, 
		    files_wfd[selected].cFileName, 
		    strlen(files_wfd[selected].cFileName));
	}
	footer_y += tm.tmHeight + tm.tmExternalLeading;
		
	int x = 0;
	char out[MAX_PATH];
	sprintf(out, "%d Kilobytes", ((files_wfd[selected].nFileSizeHigh * MAXDWORD)
				      + files_wfd[selected].nFileSizeLow) / 1024);
	TextOut(hdc, 5, footer_y, out, strlen(out));
	x = strlen(out) * tm.tmMaxCharWidth + 8;
	if(files_wfd[selected].dwFileAttributes & FILE_ATTRIBUTE_READONLY){
	    TextOut(hdc, x, footer_y, "/ Read only", 11);
	    x = 11 * tm.tmMaxCharWidth + 15;
	}
	sprintf(out, "(Files showen: %d)", file_list);
	TextOut(hdc, x, footer_y, out, strlen(out)); 
	footer_y += tm.tmHeight + tm.tmExternalLeading;
	if(f_copy_selected == 1){
	    sprintf(out, "Copying: %s", copy_filename);
	    SetTextColor(hdc, RGB(255, 255, 255));
	    TextOut(hdc, 5, footer_y, out, strlen(out));		
	}
	free(files_wfd);
	EndPaint(hwnd, &paint_class);
    }
    else if(message == WM_KEYDOWN){
	char dir_astrick[MAX_PATH+1];
	sprintf(dir_astrick, "%s*", selected_dir);
	if(file_list != 0){
	    if((wParam == VK_UP) && (selected != 0)){
		selected--;
		if(selected < files_start){
		    files_start--;
		}
		InvalidateRect(hwnd, NULL, 1);
	    }
	    else if((wParam == VK_DOWN) && (selected != (file_list-1))) {
		selected++;
		if(selected == (files_start + files_per_area)){
		    files_start++;
		}
		InvalidateRect(hwnd, NULL, 1);
	    }
	}
	if(wParam == VK_RETURN){
	    if(f_show_addressbar == 1 && (strlen(dir_input) != 0)){
		int length = strlen(dir_input)-1;
		char * input_astrick = (char *) malloc(strlen(dir_input)+2);
		if(input_astrick == NULL){
		    error("Not enough memory.");
		}
		strcpy(input_astrick, dir_input);
		if(input_astrick[length] != '\\'){
		    input_astrick[length+1] = '\\';
		}
		length = strlen(dir_input)+1;
		input_astrick[length] = '*';
		input_astrick[length+1] = 0;

		if(file_exists(dir_input) && get_file_count(input_astrick) == -1){
		    ShellExecute(NULL, NULL, dir_input, "", selected_dir, SW_SHOWNORMAL);
		}
		else if(get_file_count(input_astrick) < 0){
		    MessageBox (NULL, "Invalid path.", app_name, MB_ICONERROR);
		}
		else{
		    if(history_id == MAX_HISTORY){
			history_id = 0;
		    }
		    strcpy(history[history_id].path, selected_dir);
		    history_id++;
		    strcpy(selected_dir, dir_input);
		    *search_input = 0;
		    f_show_addressbar = 0;
		    InvalidateRect(hwnd, NULL, 1);
		}
		free(input_astrick);
	    }
	    else if(f_selected_dir == 1) {
		if(history_id == MAX_HISTORY){
		    history_id = 0;
		}
		strcpy(history[history_id].path, selected_dir);
		history_id++;
		if(!strcmp(selected_file, "..")){
		    char parent[MAX_PATH];
		    get_parent_dir(selected_path, parent);
		    if(parent == NULL){
			sprintf(selected_dir, "%s..", selected_path);
		    }
		    else{
			sprintf(selected_dir, "%s", parent);
		    }
		}
		else{
		    sprintf(selected_dir, "%s\\", selected_path);
		}
		selected = 0;
		files_start = 0;
		*search_input = 0;
		InvalidateRect(hwnd, NULL, 1);
	    }
	    else{
		ShellExecute(NULL, NULL, selected_path, "", selected_dir, SW_SHOWNORMAL);
	    }
	}
	else if(wParam == VK_DELETE){
	    if(!DeleteFile(selected_path)){
		char formulated[513];
		char message[530];

		FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, 0, GetLastError(), 0,
			      formulated, 512, NULL);
		sprintf(message, "%s", formulated);
		MessageBox (NULL, message, app_name, MB_ICONERROR);
	    }
	    else if(selected != 0){
		selected--;
		if(selected < files_start){
		    files_start--;
		}
	    }
	    InvalidateRect(hwnd, NULL, 1);	
	}
	else if(wParam == VK_END){
	    selected = file_list-1;
	    if(file_list >= files_per_area){
		files_start = file_list - files_per_area;
	    }
	    else{
		files_start = 0;
	    }
	    InvalidateRect(hwnd, NULL, 1);
	}
	else if(wParam == VK_HOME){
	    selected = 0;
	    files_start = 0;
	    InvalidateRect(hwnd, NULL, 1);
	}
	else if(wParam == VK_BACK && (f_show_addressbar != 1 && f_show_search != 1)){
	    if(history_id >= 1){
		history_id--;
		strcpy(selected_dir, history[history_id].path);
		selected = 0;
		files_start = 0;
		*search_input = 0;
		InvalidateRect(hwnd, NULL, 1);
	    }
	}
	else if((GetAsyncKeyState(VK_CONTROL) < 0 ) && wParam == 70){
	    if(f_show_search == 1){
		*search_input = 0;
		f_show_search = 0;
	    }
	    else{
		f_show_search = 1;
	    }
	    f_ignore_char = 1;
	    InvalidateRect(hwnd, NULL, 1);
	}
	else if((GetAsyncKeyState(VK_CONTROL) < 0 ) && wParam == 76){
	    if(f_show_addressbar == 1){
		f_show_addressbar = 0;
	    }
	    else{
		f_show_search = 0;
		*search_input = 0;
		memset(dir_input, 0, MAX_PATH+3);
		f_show_addressbar = 1;
	    }
	    f_ignore_char = 1;
	    strcpy(dir_input, selected_dir);
	    InvalidateRect(hwnd, NULL, 1);
	}
	else if((GetAsyncKeyState(VK_CONTROL) < 0 ) && wParam == 67 && selected != 0){
	    f_copy_selected = 1;
	    strcpy(copy_filename, selected_file);
	    strcpy(copy_path, selected_path);
	    InvalidateRect(hwnd, NULL, 1);
	}
	else if((GetAsyncKeyState(VK_CONTROL) < 0 ) && wParam == 86 && f_copy_selected == 1){
	    char temp[MAX_PATH+5];
	    if(f_selected_dir){
		sprintf(temp, "%s\\%s", selected_path, copy_filename);
	    }
	    else{
		sprintf(temp, "%s\\%s", selected_dir, copy_filename);
	    }

	    if((!strcmp(selected_path, copy_path) && f_selected_dir)) {
		MessageBox(NULL, "Cannot copy folder into itself.", app_name, MB_ICONERROR);
	    }
	    else if(!file_exists(copy_path)){
		MessageBox(NULL, "File does not exist anymore.", app_name, MB_ICONERROR);
	    }
	    else if(file_exists(temp)) {
		char * cerror = (char *) malloc(MAX_PATH * 2 + 50);
		sprintf(cerror, "Do you want to replace:\n  %s (%.2f mb)\n\nwith:\n  %s (%.2f mb)?",
			copy_filename, get_file_size(temp), copy_filename, get_file_size(copy_path));
		int answer = MessageBox(NULL, cerror, app_name, MB_YESNO | MB_ICONQUESTION);
		if(answer == IDYES){
		    CopyFileEx(copy_path, temp, &CopyProgressRoutine, NULL, 0, COPY_FILE_RESTARTABLE );
		}
		free(cerror);
		ongoing_copy++; 
		f_copy_selected = 0;
		InvalidateRect(hwnd, NULL, 1);
	    }
	    else{
		if(!CopyFileEx(copy_path, temp, &CopyProgressRoutine, NULL, 0, COPY_FILE_RESTARTABLE)) {
		    error("from:CopyFileEx");
		}				
		f_copy_selected = 0;
		ongoing_copy++;
		InvalidateRect(hwnd, NULL, 1);
	    }
	}
    }
    else if(message == WM_CHAR){
	if(f_ignore_char == 1){
	    f_ignore_char = 0;
	}
	else if(f_show_search == 1){
	    int length = strlen(search_input);
	    if(wParam == '\b'){
		search_input[length-1] = 0;
	    }
	    else if(length <= MAX_SEARCH_INPUT && wParam != '\r' && isprint(wParam)){
		search_input[length] = wParam;
		search_input[length+1] = 0;
	    }
	    files_start = 0;
	    selected = 0;
	    InvalidateRect(hwnd, NULL, 1);
	}
	else if(f_show_addressbar == 1){
	    int length = strlen(dir_input);
	    if(wParam == '\b'){
		dir_input[length-1] = 0;
	    }
	    else if(length <= MAX_PATH && wParam != '\r' && isprint(wParam)){
		dir_input[length] = wParam;
		dir_input[length+1] = 0;
	    }
	    InvalidateRect(hwnd, NULL, 1);
	}
    }
    else if(message == WM_MOUSEWHEEL) {
	if(LOWORD(wParam) == MK_CONTROL){
	    SLWA SetLayeredWindowAttributes = 0;
	    HMODULE hDLL = GetModuleHandle("USER32.DLL");
	    SetLayeredWindowAttributes = 
		(SLWA)GetProcAddress(hDLL, "SetLayeredWindowAttributes");
	    if(SetLayeredWindowAttributes == 0){
		error("from:SetLayeredWindowAttributes");
	    }
	    if(((short)HIWORD(wParam) > 0) && (user_config.transparency < 255)){
		user_config.transparency += 5;
	    }
	    else if(((short)HIWORD(wParam) < 0) && user_config.transparency != 25){
		user_config.transparency -= 5;
	    }
	    SetLayeredWindowAttributes(hwnd, 0, user_config.transparency,
				       LWA_ALPHA);
	    FreeLibrary(hDLL);
	}
	else if(file_list != 0){
	    char dir_astrick[MAX_PATH+1];
	    sprintf(dir_astrick, "%s*", selected_dir);
	    if(((short)HIWORD(wParam) > 0) && selected != 0){
		selected--;
		if(selected < files_start ){
		    files_start--;
		}
		InvalidateRect(hwnd, NULL, 1);	
	    }
	    else if(((short)HIWORD(wParam) < 0) && selected != (file_list-1)) {
		selected++;
		if(selected == (files_start + files_per_area)){
		    files_start++;
		}
		InvalidateRect(hwnd, NULL, 1);
	    }
	}
    }
    else if(message == WM_MOVE){
	user_config.pos_y = LOWORD(lParam );
	user_config.pos_y = HIWORD(lParam);
    }
    else if(message == WM_LBUTTONDOWN && (GetAsyncKeyState(VK_CONTROL) < 0 )){
	SetCursor (LoadCursor (NULL, IDC_SIZEALL));
	ShowCursor (TRUE);
	ReleaseCapture();
	SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    }
    else if(message == WM_DESTROY){
	PostQuitMessage(0);
	return 0;
    }
    else if(message == WM_CLOSE){
	DestroyWindow(hwnd);
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
}

int get_file_count(char dir[MAX_PATH]){
    WIN32_FIND_DATA file_count;
    int files = 0;
    HANDLE fh = FindFirstFile(dir, &file_count);
    if(fh == INVALID_HANDLE_VALUE){
	FindClose(fh);
	return -1;
    }
    while(FindNextFile(fh, &file_count)){
	files++;
    }
    FindClose(fh);
    return files;
}

BOOL file_exists(char dir[MAX_PATH]){ // returns 1 if file exits
    WIN32_FIND_DATA temp;
    HANDLE fh = FindFirstFile(dir, &temp);
    if(fh == INVALID_HANDLE_VALUE){
	return 0;
    }
    FindClose(fh);
    return 1;
}

float get_file_size(char path[MAX_PATH]){
    WIN32_FIND_DATA file;
    HANDLE fh = FindFirstFile(path, &file);
    if(fh == INVALID_HANDLE_VALUE){
	notify(path);
	error("error:get_file_size/FindFirstFile");
    }
    FindClose(fh);
    return (float) ((file.nFileSizeHigh * MAXDWORD) + file.nFileSizeLow) / 1048576;
}

void error(char * error){
    char message[530];
    memset(message, 0, 530);
    if(!strncmp(error, "from:", 5)){
	error += 5;
	char formulated[513];
	memset(formulated, 0, 513);
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, 0, GetLastError(), 0,
		      formulated, 512, NULL);
	sprintf(message, "%s: %s", error, formulated);
    }
    else{
	sprintf(message, "%s", error);
    }
    MessageBox (NULL, message, app_name, MB_ICONERROR);
    exit(1);
}

void get_parent_dir(char dirc[MAX_PATH], char * parent){
    char dir[MAX_PATH]; // local copy
    strcpy(dir, dirc);
    int seek = 0;

    while(dir[seek] != 0){
	seek++;
    }
    seek--;
    if(dir[seek] == '.' && dir[seek-1] == '.'){
	dir[seek] = 0;
	dir[seek-1] = 0;
    }
    seek -= 2;
    if(dir[seek] == '\\'){
	dir[seek] = 0;
    }

    while(dir[seek] != '\\'){
	if(seek == 0){
	    *parent = 0;
	    return;
	}
	dir[seek] = 0;
	seek--;
    }
    dir[seek] = '\\';
    strcpy(parent, dir);
}

void strlower(char * str){
    int length = strlen(str);
    while(length--){
	*str = tolower(*str);
	str++;
    }
}

DWORD WINAPI t_dir_change(PVOID param){
    f_dir_change_executing = 1;
    HANDLE hchange = FindFirstChangeNotification(selected_dir, 0, FILE_NOTIFY_CHANGE_FILE_NAME |
						 FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_ATTRIBUTES);
    if(hchange == INVALID_HANDLE_VALUE){
	error("from:FindFirstChangeNotification");
    }
    if(WaitForSingleObject(hchange, INFINITE) == WAIT_OBJECT_0){
	f_dir_change_executing = 0;
	InvalidateRect(hwnd, NULL, 1);
    }
    FindClose(hchange);
    return 1;
}

DWORD WINAPI CopyProgressRoutine(LARGE_INTEGER total_file_size, LARGE_INTEGER total_transferred,
				 LARGE_INTEGER stream, LARGE_INTEGER stream_transferred, DWORD current_stream, DWORD reason, 
				 HANDLE source, HANDLE dest, LPVOID data){
    if(total_file_size.QuadPart == total_transferred.QuadPart){
	memset(&copy_progress[ongoing_copy], 0, sizeof(copy_progress));
	ongoing_copy--;
    }
    else if(reason == CALLBACK_STREAM_SWITCH){
	copy_progress[ongoing_copy].done = 0;
	copy_progress[ongoing_copy].file = dest;
	copy_progress[ongoing_copy].kb_copied = 0;
	copy_progress[ongoing_copy].kb_full = total_file_size.QuadPart / 1024;
    }
    else if(ongoing_copy != 0){
	int t = 0;
	while(t != ongoing_copy){
	    if(copy_progress[t].file == dest && (total_transferred.QuadPart > 1024)){
		copy_progress[t].done = ((total_transferred.QuadPart / 1024) / 
					 (total_file_size.QuadPart / 1024) * 100);
		copy_progress[t].kb_copied = total_transferred.QuadPart / 1024;
	    }
	}
    }
    notify("CALLED");
    InvalidateRect(hwnd, NULL, 1);
    return PROGRESS_CONTINUE;
}

// for testing, remove for production:

void notify(char * message){
    MessageBox (NULL, message, app_name, NULL);
}

void notifyd(int d){
    char message[20];
    sprintf(message, "%d", d);
    MessageBox (NULL, message, app_name, NULL);
}

void notifyc(char c){
    char message[3];
    sprintf(message, "%c", c);
    MessageBox (NULL, message, app_name, NULL);
}

