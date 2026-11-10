/* Visible menu only. Legacy IDs remain stable for internal state handling. */
#ifndef NXL_SRW_LAYOUT_H
#define NXL_SRW_LAYOUT_H
#define NXL_LENS_TEST_UI 1
static const unsigned char nxl_srw_visible_rows[]={0,2,13,3,4,5,9
#if NXL_LENS_TEST_UI
 ,10,11,12
#endif
};
static int nxl_srw_ui_count(void){return (int)(sizeof nxl_srw_visible_rows/sizeof *nxl_srw_visible_rows);}
static int nxl_srw_ui_index(int id){for(int i=0;i<nxl_srw_ui_count();i++)if(nxl_srw_visible_rows[i]==id)return i;return -1;}
static int nxl_srw_ui_at(int index){int count=nxl_srw_ui_count();return nxl_srw_visible_rows[(index%count+count)%count];}
static int nxl_srw_ui_touch(int cursor,int y){if(cursor<0||cursor>=nxl_srw_ui_count()||y<104||y>=384)return -1;int row=cursor/5*5+(y-104)/56;return row<nxl_srw_ui_count()?row:-1;}
#endif
