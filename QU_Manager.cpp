#include "StdAfx.h"
#include "QU_Manager.h"
#include "SYS_Manager.h"
#include "RM_Manager.h"

BOOL IsNeedRec(RM_Record *rec,int n,Con *selections);  //判断记录是否符合其他要求

void Init_Result(SelResult * res){
	res->next_res = NULL;
}

void Destory_Result(SelResult * res){
	for(int i = 0;i<res->row_num;i++){
/*		for(int j = 0;j<res->col_num;j++){
			delete[] res->res[i][j];
		}*/
		delete[] res->res[i];
	}
	if(res->next_res != NULL){
		Destory_Result(res->next_res);
	}
}

RC Query(char * sql,SelResult * res){//调用查询
	RC rc;
	sqlstr *sql_str=NULL;
	sql_str=get_sqlstr();
	if(rc=parse(sql,sql_str))
		return rc;
	rc=Select (sql_str->sstr.sel.nSelAttrs,sql_str->sstr.sel.selAttrs,sql_str->sstr.sel.nRelations,sql_str->sstr.sel.relations,sql_str->sstr.sel.nConditions,sql_str->sstr.sel.conditions,res);
	return rc;
}

RC singletable_nocon(int nSelAttrs,RelAttr **selAttrs,int nRelations,char **relations,SelResult * res)
{
	RC rc;
	SelResult *result=res;//存放结果
	RM_FileScan *scan=new RM_FileScan;
	scan->bOpen=false;
	RM_FileHandle *table=new RM_FileHandle;//扫描表文件获取属性个数
	table->bOpen=false;
	RM_Record *rtable=new RM_Record;
	rtable->bValid=false;
	if(rc=RM_OpenFile("SYSTABLES",table))
		return rc;
	Con *condi = (Con*)malloc(sizeof(Con));
	condi->attrType=chars;
	condi->bLhsIsAttr = 1; 
	condi->LattrOffset = 0; 
 	condi->LattrLength = strlen(*relations) + 1; 
	condi->compOp = EQual; 
	condi->bRhsIsAttr = 0; 
 	condi->Rvalue = *relations; 
	OpenScan(scan,table,1,condi);
	GetNextRec(scan,rtable);
	memcpy(&(result->col_num),rtable->pData+21,sizeof(int));//属性数
	/*if(nSelAttrs!=result->col_num)
	{
		result->col_num=nSelAttrs;
		return SQL_SYNTAX;
	}*/
	CloseScan(scan);
	RM_CloseFile(table);
	if(rc=RM_OpenFile("SYSCOLUMNS",table))//扫描列文件获取属性信息
		return rc;
	OpenScan(scan,table,1,condi);
	char *tmp=NULL;
	for(int i=0;i<result->col_num;i++)
	{
		GetNextRec(scan,rtable);
		tmp=rtable->pData;
		memcpy(&result->fields[i],tmp+21,21);//名称
		memcpy(&result->type[i],tmp+42,sizeof(AttrType));//数据类型
		memcpy(&result->length[i],tmp+42+sizeof(AttrType),4);//长度
		memcpy(&result->offset[i],tmp+50,4);//长度
	}
	CloseScan(scan);
	RM_CloseFile(table);
	if(rc=RM_OpenFile(*relations,table))//打开表文件获取信息
		return rc;
	OpenScan(scan,table,0,NULL);
	result->row_num=0;
	SelResult *tmpr=result;//插入新节点
	int i=0;
	while(!GetNextRec(scan,rtable))
	{
		if(tmpr->row_num>=100)//每个节点100个记录
		{
			tmpr->next_res=(SelResult*)malloc(sizeof(SelResult));
			tmpr->next_res->col_num=tmpr->col_num;
			for(int j=0;j<tmpr->col_num;j++)
			{
				strncpy(tmpr->next_res->fields[i], tmpr->fields[i], strlen(tmpr->fields[i])); 
				tmpr->next_res->type[i] = tmpr->type[i]; 
				tmpr->next_res->length[i] = tmpr->length[i];
			}
			tmpr=tmpr->next_res;
			tmpr->next_res=NULL;
			tmpr->row_num = 0;
		}
		tmpr->res[tmpr->row_num]=(char **)malloc(sizeof(char *)); 
	 	*(tmpr->res[tmpr->row_num++])=rtable->pData; 
	}
	CloseScan(scan);
	RM_CloseFile(table);
	free(scan);
	free(table);
	res=result;
	return SUCCESS;
}

RC singletable_cons(int nSelAttrs,RelAttr **selAttrs,int nRelations,char **relations,int nConditions,Condition *conditions,SelResult * res){
	SelResult *resHead = res;
	RM_FileHandle *col_Handle,*data_Handle;
	RM_FileScan *FileScan;
	RM_Record *record = (RM_Record *)malloc(sizeof(RM_Record));
	RM_Record *data_rec = (RM_Record *)malloc(sizeof(RM_Record));
	record->bValid = false;
	data_rec->bValid = false;

	Con cons[2];
	cons[0].attrType = chars;
	cons[0].bLhsIsAttr = 1;
	cons[0].bRhsIsAttr = 0;
	cons[0].compOp = EQual;
	cons[0].LattrOffset = 0;
	cons[0].LattrLength = 21;
	cons[0].Rvalue = *relations;

	cons[1].attrType = chars;
	cons[1].bLhsIsAttr = 1;
	cons[1].bRhsIsAttr = 0;
	cons[1].compOp = EQual;
	cons[1].LattrOffset = 21;
	cons[1].LattrLength = 21;

	col_Handle = (RM_FileHandle *)malloc(sizeof(RM_FileHandle));
	FileScan = (RM_FileScan *)malloc(sizeof(RM_FileScan));


	char indexName[21];//索引名称
	int indexExist = 0;//索引存在判断
	CompOp op;
	void *value = NULL;
	int flag =0;  //取第一个有索引的属性作为带索引查询的扫描条件，并忽略其他索引

	//获取属性名、属性长度、属性偏移量、属性类型、属性数量。并判断是否有属性有索引
	if (!strcmp((*selAttrs)->attrName, "*")){//查询结果为所有属性
		if (RM_OpenFile("SYSCOLUMNS", col_Handle)!= SUCCESS) return SQL_SYNTAX;
		OpenScan(FileScan, col_Handle, 1, &cons[0]);
		int i = 0;
		while(GetNextRec(FileScan, record)== SUCCESS){
			char * column = record->pData;
			memcpy(&resHead->type[i], column + 42, sizeof(int));
			memcpy(&resHead->fields[i], column + 21, 21);
			memcpy(&resHead->offset[i], column + 50, sizeof(int));
			memcpy(&resHead->length[i], column + 46, sizeof(int));
			i++;
		}
		resHead->col_num = i;
		CloseScan(FileScan);
		RM_CloseFile(col_Handle);
	}else{
		resHead->col_num = nSelAttrs;
		if(RM_OpenFile("SYSCOLUMNS", col_Handle)!= SUCCESS) return SQL_SYNTAX;

    	for (int i = 0; i < nSelAttrs; i++){
		    cons[1].Rvalue = (selAttrs[nSelAttrs - i - 1])->attrName;
		    OpenScan(FileScan, col_Handle, 2, cons);
		    if (GetNextRec(FileScan, record)!= SUCCESS) return SQL_SYNTAX;
		    char * column = record->pData;
		    memcpy(&resHead->type[i], column + 42, sizeof(int));
		    memcpy(&resHead->fields[i], column + 21, 21);
		    memcpy(&resHead->offset[i], column + 50, sizeof(int));
		    memcpy(&resHead->length[i], column + 46, sizeof(int));
		    CloseScan(FileScan);
	    }
	    RM_CloseFile(col_Handle);
	}

	//获取扫描数据表的扫描条件
	Con *selectCons = (Con *)malloc(sizeof(Con) * nConditions);
	for(int i=0;i<nConditions;i++){
		if (conditions[i].bLhsIsAttr == 0 && conditions[i].bRhsIsAttr == 1){//左边是值，右边是属性
			cons[1].Rvalue = conditions[i].rhsAttr.attrName;
		}
		else if (conditions[i].bLhsIsAttr == 1 && conditions[i].bRhsIsAttr == 0){//左边是属性，右边是值
			cons[1].Rvalue = conditions[i].lhsAttr.attrName;
		}

		RM_OpenFile("SYSCOLUMNS", col_Handle);
		OpenScan(FileScan, col_Handle, 2, cons);
		GetNextRec(FileScan,record);

		//索引判断
		if(record->pData[54]=='1') {
			indexExist = 1;
			memcpy(indexName,record->pData+55,21);
		}
			

		selectCons[i].bLhsIsAttr = conditions[i].bLhsIsAttr;
		selectCons[i].bRhsIsAttr = conditions[i].bRhsIsAttr;
		selectCons[i].compOp = conditions[i].op;

		if (conditions[i].bLhsIsAttr == 1) //左边属性
		{ //设置属性长度和偏移量
			memcpy(&selectCons[i].LattrLength, record->pData + 46, 4);
			memcpy(&selectCons[i].LattrOffset, record->pData + 50, 4);
			selectCons[i].attrType = conditions[i].rhsValue.type;
			selectCons[i].Rvalue = conditions[i].rhsValue.data;
			if(indexExist==1&&flag==0){
				value = conditions[i].rhsValue.data;
				op = conditions[i].op;
				flag = 1;
			}
		}
		else {
			selectCons[i].attrType = conditions[i].lhsValue.type;
			selectCons[i].Lvalue = conditions[i].lhsValue.data;
			memcpy(&selectCons[i].RattrLength, record->pData + 46, 4);
			memcpy(&selectCons[i].RattrOffset, record->pData + 50, 4);
			if(indexExist==1&&flag==0){
				value = conditions[i].rhsValue.data;
				op = conditions[i].op;
				flag = 1;
			}
		}
		CloseScan(FileScan);
	}
	RM_CloseFile(col_Handle);

	data_Handle =(RM_FileHandle *)malloc(sizeof(RM_FileHandle));
    RM_OpenFile(*relations, data_Handle);

	if(indexExist){
		//利用索引进行查询
		RID *rid=new RID;
		rid->bValid = false;
		IX_IndexHandle *indexhandle=new IX_IndexHandle;
		indexhandle->bOpen=false;
		IX_IndexScan *indexscan=new IX_IndexScan;
		indexscan->bOpen=false;
		OpenIndex (indexName,indexhandle);//打开索引文件
		OpenIndexScan(indexscan, indexhandle, op, (char*)value);//初始化条件扫描

		int i = 0;
    	resHead->row_num = 0;
	    SelResult *tail = resHead;  //尾插法向链表中插入新结点

		while(IX_GetNextEntry(indexscan,rid)==SUCCESS){
				GetRec(data_Handle,rid,data_rec);
				if(IsNeedRec(data_rec,nConditions,selectCons)){
					if(tail->row_num >=100){
						tail->next_res = (SelResult *)malloc(sizeof(SelResult));
		            	tail->next_res->col_num = tail->col_num;
						for(int j= 0;j< tail->col_num;j++){
							memcpy(tail->next_res->fields[j],tail->fields[j],sizeof(tail->fields[j]));
			               	tail->next_res->type[j] = tail->type[j];
			            	tail->next_res->offset[j] = tail->offset[j];
			            	tail->next_res->length[j] = tail->length[j];
						}
						tail=tail->next_res;
		    	        tail->col_num = 0;
		    	        tail->next_res = NULL;
					}
					tail->res[tail->row_num] = (char **)malloc(sizeof(char *));
	    	       *(tail->res[tail->row_num++]) = data_rec->pData;
				}
		}
		CloseIndexScan(indexscan);//关闭扫描
		CloseScan(FileScan);
    	res = resHead;
	}
	else{
    	OpenScan(FileScan, data_Handle, nConditions, selectCons);
	    int i = 0;
    	resHead->row_num = 0;
	    SelResult *tail = resHead;  //尾插法向链表中插入新结点
    	while (GetNextRec(FileScan, data_rec) == SUCCESS){
	    	if(tail->row_num >=100){  //超过100条记录
		    	tail->next_res = (SelResult *)malloc(sizeof(SelResult));
		    	tail->next_res->col_num = tail->col_num;
		    	for(int j= 0;j< tail->col_num;j++){
			    	memcpy(tail->next_res->fields[j],tail->fields[j],sizeof(tail->fields[j]));
			      	tail->next_res->type[j] = tail->type[j];
			    	tail->next_res->offset[j] = tail->offset[j];
			    	tail->next_res->length[j] = tail->length[j];
			      }
		    	tail=tail->next_res;
		    	tail->col_num = 0;
		    	tail->next_res = NULL;
	    	}
	    	tail->res[tail->row_num] = (char **)malloc(sizeof(char *));
	    	*(tail->res[tail->row_num++]) = data_rec->pData;
    	}
    	CloseScan(FileScan);
    	res = resHead;
    	}
	
	return SUCCESS;
}


RC mutiresult(int nSelAttrs,RelAttr **selAttrs,int nRelations,char **relations,int nConditions,Condition *conditions,SelResult * res,int tablenum,char *tem,int *offset)
{
	if(tablenum<0)//遍历完全部表 完成一条记录的查询
	{
		SelResult *tail = res;  //尾插法向链表中插入新结点
		if(tail->row_num >=100){  //超过100条记录
			tail->next_res = (SelResult *)malloc(sizeof(SelResult));
			tail->next_res->col_num = tail->col_num;
			for(int j= 0;j< tail->col_num;j++){//将属性信息传给下一节点
				memcpy(tail->next_res->fields[j],tail->fields[j],sizeof(tail->fields[j]));
				tail->next_res->type[j] = tail->type[j];
				tail->next_res->offset[j] = tail->offset[j];
				tail->next_res->length[j] = tail->length[j];
			}
			tail=tail->next_res;
			tail->col_num = 0;
			tail->next_res = NULL;
		}
		tail->res[tail->row_num] = (char **)malloc(sizeof(char *));
		*(tail->res[tail->row_num]) = (char *)malloc(sizeof(char)*offset[nRelations]);
		memcpy(*(tail->res[tail->row_num]), tem, offset[nRelations]);
		tail->row_num++;
		return SUCCESS;
	}
	RC rc;
	RelAttr connecttable;//需连接表的信息
	int ntable=0;
	int op=0;
	Con **tablecons=(Con**)malloc(sizeof(Con*)*nConditions);
	int ncons=0;
	RM_FileHandle *handle=new RM_FileHandle;
	handle->bOpen=false;
	RM_Record  *rec=new RM_Record;
	rec->bValid=false;
	RM_FileScan *scan=new RM_FileScan;
	scan->bOpen=false;
	Con cons[2];
	cons[0].attrType = chars;//查表名条件
	cons[0].bLhsIsAttr = 1;
	cons[0].bRhsIsAttr = 0;
	cons[0].compOp = EQual;
	cons[0].LattrOffset = 0;
	cons[0].LattrLength = 21;
	cons[0].Rvalue = relations[tablenum];
	cons[1].attrType = chars;//查表名条件
	cons[1].bLhsIsAttr = 1;
	cons[1].bRhsIsAttr = 0;
	cons[1].compOp = EQual;
	cons[1].LattrOffset = 21;
	cons[1].LattrLength = 21;
	RM_OpenFile("SYSCOLUMNS", handle);
	for(int i=0;i<nConditions;i++)
	{
		if(conditions[i].bLhsIsAttr==1&&conditions[i].bRhsIsAttr == 1)//属性=属性
		{
			
			if (!strcmp(conditions[i].lhsAttr.relName,relations[tablenum]))//左边为当前表属性
 			{ 
 				cons[1].Rvalue = conditions[i].lhsAttr.attrName; //条件处理
 				connecttable.relName = conditions[i].rhsAttr.relName; 
				connecttable.attrName = conditions[i].rhsAttr.attrName; 
 			} 
			else if (!strcmp(conditions[i].rhsAttr.relName,relations[tablenum]))//右边为当前表属性
 			{ 
 				cons[1].Rvalue = conditions[i].rhsAttr.attrName; 
 				connecttable.relName = conditions[i].lhsAttr.relName; 
				connecttable.attrName = conditions[i].lhsAttr.attrName; 
 			} 
			else continue;//条件与当前表无关
			int j;
			for(j=0;j<nRelations;j++)//找到需连接表位置
			{
				if(!strcmp(relations[nRelations-j-1],connecttable.relName))
				break;
			}
			if(nRelations-j-1<tablenum)//只需考虑还未处理过的表
				continue;
			ntable=j;
			op=1;
		}
		else if(conditions[i].bLhsIsAttr==1)//属性=值
		{
			if(!strcmp(conditions[i].lhsAttr.relName, relations[tablenum]))
			{
				cons[1].Rvalue = conditions[i].lhsAttr.attrName;
				op=2;
			}
			else	continue;

		}
		else//值=属性
		{
			if(strcmp(conditions[i].rhsAttr.relName, relations[tablenum]))
			{
				cons[1].Rvalue = conditions[i].rhsAttr.attrName; 
				op=3;
			}
			else	continue;
		}
		OpenScan(scan,handle,2,cons);
		GetNextRec(scan,rec);
		tablecons[ncons]=(Con*)malloc(sizeof(Con));
		switch(op)
		{
		case 1://属性=属性
					{
			RM_FileHandle *temhandle=new RM_FileHandle;
			temhandle->bOpen=false;
			RM_FileScan *temscan=new RM_FileScan;
			temscan->bOpen=false;
			RM_Record *temrec=new RM_Record;
			temrec->bValid=false;
			(*tablecons+ncons)->bLhsIsAttr = 1; //左边为当前表
		    (*tablecons+ncons)->bRhsIsAttr = 0;  		 
			(*tablecons+ncons)->compOp = conditions[i].op;  
 		   	memcpy(&(*tablecons+ncons)->LattrLength, rec->pData + 46, 4); //记录当前表信息
 		   	memcpy(&(*tablecons+ncons)->LattrOffset, rec->pData + 50, 4); 
			memcpy(&(*tablecons+ncons)->attrType, rec->pData + 42, 4);
			RM_OpenFile("SYSCOLUMNS",temhandle);
			cons[0].Rvalue = connecttable.relName; 
			cons[1].Rvalue = connecttable.attrName; 
			OpenScan(temscan,temhandle,2,cons);
			GetNextRec(temscan,temrec);//找到待连接表记录 获取目标数据偏移量
			int inoffset;
			memcpy(&inoffset,temrec->pData+50,4);
			memcpy(&((*tablecons+ncons)->attrType),temrec->pData+42,4);//类型
			(*tablecons+ncons)->Rvalue=tem+offset[ntable]+inoffset;//获取待连接表记录值 总表起始地址+表内偏移量
			CloseScan(temscan);
			free(temscan);
			free(temhandle);
			free(temrec);
		} 	
			break;
		case 2://属性=值
			(*tablecons+ncons)->bLhsIsAttr = 1; 
		    (*tablecons+ncons)->bRhsIsAttr = 0;  		 
			(*tablecons+ncons)->compOp = conditions[i].op;  //操作符
			memcpy(&(*tablecons+ncons)->LattrLength, rec->pData + 46, 4); 
			memcpy(&(*tablecons+ncons)->LattrOffset, rec->pData + 50, 4); 
 			(*tablecons+ncons)->attrType = conditions[i].rhsValue.type; 
			(*tablecons+ncons)->Rvalue = conditions[i].rhsValue.data; 
			break;
		case 3://值=属性
			(*tablecons+ncons)->bLhsIsAttr = 0; 
			(*tablecons+ncons)->bRhsIsAttr = 1; 	
			(*tablecons+ncons)->compOp = conditions[i].op; 
			memcpy(&(*tablecons+ncons)->RattrLength, rec->pData + 46, 4); //长度
 		   	memcpy(&(*tablecons+ncons)->RattrOffset, rec->pData + 50, 4); //偏移量
		   	(*tablecons+ncons)->attrType = conditions[i].lhsValue.type; //类型
		   	(*tablecons+ncons)->Lvalue = conditions[i].lhsValue.data; //数据
			break;
		default :
			return FAIL;
		}
		CloseScan(scan);
		ncons++;//筛选出的条件加一
	}
	RM_CloseFile(handle);
	RM_OpenFile(relations[tablenum],handle);
	OpenScan(scan,handle,ncons,*tablecons);
	while(!GetNextRec(scan,rec))//递归调用 完善每一条记录
	{
		char *temresult=(char*)malloc(offset[nRelations-tablenum]*sizeof(char));
		if(tem)
		memcpy(temresult,tem,offset[nRelations-tablenum-1]); 
		memcpy(temresult+offset[nRelations-tablenum-1], rec->pData, handle->subhead->recordSize); 
		mutiresult(nSelAttrs,selAttrs,nRelations,relations,nConditions,conditions,res,tablenum-1,temresult,offset);//处理后续的表
	}
	CloseScan(scan);
	RM_CloseFile(handle);
	free(scan);
	free(handle);
	free(rec);
	return SUCCESS;
}

RC multitable_nocon(int nSelAttrs,RelAttr **selAttrs,int nRelations,char **relations,int nConditions,Condition *conditions,SelResult * res)
{
	RC rc;
	int offset[10];//最多10表
	RM_FileHandle **filehandle=(RM_FileHandle**)malloc(10*(sizeof(RM_FileHandle*)));
	offset[0]=0;
	for(int i=1;i<nRelations+1;i++)
	{
		filehandle[i-1]=new RM_FileHandle;
		filehandle[i-1]->bOpen=false;
		RM_OpenFile(relations[nRelations-i],filehandle[i-1]);
		offset[i]=offset[i-1]+filehandle[i-1]->subhead->recordSize;//获取每个表结果在总表内的起始地址
		RM_CloseFile(filehandle[i-1]);
		free(filehandle[i-1]);
	}
	free(filehandle);

	SelResult *resHead = res;
	RM_FileHandle *handle=new RM_FileHandle;
	handle->bOpen=false;
	RM_Record  *rec=new RM_Record;
	rec->bValid=false;
	RM_FileScan *scan=new RM_FileScan;
	scan->bOpen=false;
	Con cons[2];
	cons[0].attrType = chars;//查表名条件
	cons[0].bLhsIsAttr = 1;
	cons[0].bRhsIsAttr = 0;
	cons[0].compOp = EQual;
	cons[0].LattrOffset = 0;
	cons[0].LattrLength = 21;
	cons[1].attrType = chars;//查表名条件
	cons[1].bLhsIsAttr = 1;
	cons[1].bRhsIsAttr = 0;
	cons[1].compOp = EQual;
	cons[1].LattrOffset = 21;
	cons[1].LattrLength = 21;
	if (!strcmp((*selAttrs)->attrName, "*")){//查询结果为所有属性
	}else{//查询结果为特定属性
		resHead->col_num = nSelAttrs;
		resHead->row_num=0;
		if(RM_OpenFile("SYSCOLUMNS", handle)!= SUCCESS) return SQL_SYNTAX;
    	for (int i = 0; i < nSelAttrs; i++){
			cons[0].Rvalue = (selAttrs[nSelAttrs - i - 1])->relName;//表名
		    cons[1].Rvalue = (selAttrs[nSelAttrs - i - 1])->attrName;//属性名
		    OpenScan(scan, handle, 2, cons);
		    if (GetNextRec(scan, rec)!= SUCCESS) return SQL_SYNTAX;
		    char * column = rec->pData;
		    memcpy(&resHead->type[i], column + 42, sizeof(int));//类型
		    memcpy(&resHead->fields[i], column + 21, 21);//名称
		    memcpy(&resHead->offset[i], column + 50, sizeof(int));//偏移量
			int j;
			for(j=0;j<nRelations;j++)
			{
				if(!strcmp(relations[nRelations-j-1],(char*)cons[0].Rvalue))
					break;
			}
			resHead->offset[i]+=offset[j];//加上表上偏移
		    memcpy(&resHead->length[i], column + 46, sizeof(int));//长度
		    CloseScan(scan);
	    }
		free(scan);
	    RM_CloseFile(handle);
		free(handle);
	}
	mutiresult(nSelAttrs,selAttrs,nRelations,relations,nConditions,conditions,res,nRelations-1,NULL,offset);
	res=resHead;
	return SUCCESS;
}




RC Select(int nSelAttrs,RelAttr **selAttrs,int nRelations,char **relations,int nConditions,Condition *conditions,SelResult * res){
	RC rc;//属性 表 条件
	int flag=0;
	RM_FileHandle *handle=new RM_FileHandle;
	handle->bOpen=false;
	RM_FileScan *scan=new RM_FileScan;
	scan->bOpen=false;
	RM_Record *rec=new RM_Record;
	if(rc=RM_OpenFile("SYSTABLES",handle))
		return rc;
	for(int i=0;i<nRelations;i++)
	{
		scan->bOpen=false;
		if(rc=OpenScan(scan,handle,0,NULL))
			return rc;
		while(true)
		{
			if(GetNextRec(scan,rec)) break;//未找到
			if(strcmp(rec->pData,relations[i])==0)//找到
			{
				flag=1;
				break;
			}
		}
		if(flag==0)
			return TABLE_NOT_EXIST;
		flag=0;
		if(rc=CloseScan(scan))
			return rc;
	}
	free(scan);
	free(handle);
	free(rec);
	if(nRelations==1)//单表查询
	{
		if(nConditions==0)//单表无条件
		{
			if(rc=singletable_nocon(nSelAttrs,selAttrs,nRelations,relations,res))//省略条件参数
				return rc;
		}
		else//单表有条件
		{
			singletable_cons(nSelAttrs,selAttrs,nRelations,relations,nConditions,conditions,res);
		}
	}
	else//多表查询
	{
		multitable_nocon(nSelAttrs,selAttrs,nRelations,relations,nConditions,conditions,res);
	}
	return rc;
}


BOOL IsNeedRec(RM_Record *rec,int n,Con *selections){
	bool flag = TRUE;  //条件不满足直接退出
	for(int i = 0;i<n;i++){
		switch(selections->attrType){
			case chars:
				char *Lattr, *Rattr;
				if (selections->bLhsIsAttr == 1)
					{ //条件的左边是属性
						Lattr = (char *)malloc(sizeof(char)*selections->LattrLength);
						memcpy(Lattr, rec->pData + selections->LattrOffset, selections->LattrLength);
						Lattr[selections->LattrLength] = '\0';
					}
					else { //条件的左边是值
						Lattr = (char *)selections->Lvalue;
					}

					if (selections->bRhsIsAttr == 1)
					{ //条件的右边是属性
						Rattr = (char *)malloc(sizeof(char)*selections->LattrLength);
						memcpy(Lattr, rec->pData + selections->RattrOffset, selections->RattrLength);
					}
					else { //条件的右边是值
						Rattr = (char *)selections->Rvalue;
					}

					flag = Compare(selections->compOp, selections->attrType, Lattr, Rattr);
					break;
			case ints:
					int *IntLattr, *IntRattr;
					if (selections->bLhsIsAttr == 1)
					{ //条件的左边是属性
						IntLattr = (int *)(rec->pData + selections->LattrOffset);
					}
					else { //条件的左边是值
						IntLattr = (int *)selections->Lvalue;
					}

					if (selections->bRhsIsAttr == 1)
					{ //条件的右边是属性
						IntRattr = (int *)(rec->pData + selections->RattrOffset);
					}
					else { //条件的右边是值
						IntRattr = (int *)selections->Rvalue;
					}

					flag = Compare(selections->compOp, selections->attrType, IntLattr, IntRattr);
					break;
				case floats:
					float *FloatLattr, *FloatRattr;
					if (selections->bLhsIsAttr == 1)
					{ //条件的左边是属性
						FloatLattr = (float *)(rec->pData + selections->LattrOffset);
					}
					else { //条件的左边是值
						FloatLattr = (float *)selections->Lvalue;
					}

					if (selections->bRhsIsAttr == 1)
					{ //条件的右边是属性
						FloatRattr = (float *)(rec->pData + selections->RattrOffset);
					}
					else { //条件的右边是值
						FloatRattr = (float *)selections->Rvalue;
					}
					flag = Compare(selections->compOp, selections->attrType, FloatLattr, FloatRattr);
					break;

				default:
					break;
		}
		if(!flag) break;  //一个条件不满足则直接跳出循环
	}
	return flag;
}