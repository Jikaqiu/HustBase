#include "StdAfx.h"
#include "QU_Manager.h"
#include "SYS_Manager.h"
#include "RM_Manager.h"

BOOL IsNeedRec(RM_Record *rec,int n,Con *selections);  //�жϼ�¼�Ƿ��������Ҫ��

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

RC Query(char * sql,SelResult * res){//���ò�ѯ
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
	SelResult *result=res;//��Ž��
	RM_FileScan *scan=new RM_FileScan;
	scan->bOpen=false;
	RM_FileHandle *table=new RM_FileHandle;//ɨ����ļ���ȡ���Ը���
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
	memcpy(&(result->col_num),rtable->pData+21,sizeof(int));//������
	/*if(nSelAttrs!=result->col_num)
	{
		result->col_num=nSelAttrs;
		return SQL_SYNTAX;
	}*/
	CloseScan(scan);
	RM_CloseFile(table);
	if(rc=RM_OpenFile("SYSCOLUMNS",table))//ɨ�����ļ���ȡ������Ϣ
		return rc;
	OpenScan(scan,table,1,condi);
	char *tmp=NULL;
	for(int i=0;i<result->col_num;i++)
	{
		GetNextRec(scan,rtable);
		tmp=rtable->pData;
		memcpy(&result->fields[i],tmp+21,21);//����
		memcpy(&result->type[i],tmp+42,sizeof(AttrType));//��������
		memcpy(&result->length[i],tmp+42+sizeof(AttrType),4);//����
		memcpy(&result->offset[i],tmp+50,4);//����
	}
	CloseScan(scan);
	RM_CloseFile(table);
	if(rc=RM_OpenFile(*relations,table))//�򿪱��ļ���ȡ��Ϣ
		return rc;
	OpenScan(scan,table,0,NULL);
	result->row_num=0;
	SelResult *tmpr=result;//�����½ڵ�
	int i=0;
	while(!GetNextRec(scan,rtable))
	{
		if(tmpr->row_num>=100)//ÿ���ڵ�100����¼
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


	char indexName[21];//��������
	int indexExist = 0;//���������ж�
	CompOp op;
	void *value = NULL;
	int flag =0;  //ȡ��һ����������������Ϊ��������ѯ��ɨ����������������������

	//��ȡ�����������Գ��ȡ�����ƫ�������������͡��������������ж��Ƿ�������������
	if (!strcmp((*selAttrs)->attrName, "*")){//��ѯ���Ϊ��������
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

	//��ȡɨ�����ݱ��ɨ������
	Con *selectCons = (Con *)malloc(sizeof(Con) * nConditions);
	for(int i=0;i<nConditions;i++){
		if (conditions[i].bLhsIsAttr == 0 && conditions[i].bRhsIsAttr == 1){//�����ֵ���ұ�������
			cons[1].Rvalue = conditions[i].rhsAttr.attrName;
		}
		else if (conditions[i].bLhsIsAttr == 1 && conditions[i].bRhsIsAttr == 0){//��������ԣ��ұ���ֵ
			cons[1].Rvalue = conditions[i].lhsAttr.attrName;
		}

		RM_OpenFile("SYSCOLUMNS", col_Handle);
		OpenScan(FileScan, col_Handle, 2, cons);
		GetNextRec(FileScan,record);

		//�����ж�
		if(record->pData[54]=='1') {
			indexExist = 1;
			memcpy(indexName,record->pData+55,21);
		}
			

		selectCons[i].bLhsIsAttr = conditions[i].bLhsIsAttr;
		selectCons[i].bRhsIsAttr = conditions[i].bRhsIsAttr;
		selectCons[i].compOp = conditions[i].op;

		if (conditions[i].bLhsIsAttr == 1) //�������
		{ //�������Գ��Ⱥ�ƫ����
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
		//�����������в�ѯ
		RID *rid=new RID;
		rid->bValid = false;
		IX_IndexHandle *indexhandle=new IX_IndexHandle;
		indexhandle->bOpen=false;
		IX_IndexScan *indexscan=new IX_IndexScan;
		indexscan->bOpen=false;
		OpenIndex (indexName,indexhandle);//�������ļ�
		OpenIndexScan(indexscan, indexhandle, op, (char*)value);//��ʼ������ɨ��

		int i = 0;
    	resHead->row_num = 0;
	    SelResult *tail = resHead;  //β�巨�������в����½��

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
		CloseIndexScan(indexscan);//�ر�ɨ��
		CloseScan(FileScan);
    	res = resHead;
	}
	else{
    	OpenScan(FileScan, data_Handle, nConditions, selectCons);
	    int i = 0;
    	resHead->row_num = 0;
	    SelResult *tail = resHead;  //β�巨�������в����½��
    	while (GetNextRec(FileScan, data_rec) == SUCCESS){
	    	if(tail->row_num >=100){  //����100����¼
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
	if(tablenum<0)//������ȫ���� ���һ����¼�Ĳ�ѯ
	{
		SelResult *tail = res;  //β�巨�������в����½��
		if(tail->row_num >=100){  //����100����¼
			tail->next_res = (SelResult *)malloc(sizeof(SelResult));
			tail->next_res->col_num = tail->col_num;
			for(int j= 0;j< tail->col_num;j++){//��������Ϣ������һ�ڵ�
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
	RelAttr connecttable;//�����ӱ����Ϣ
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
	cons[0].attrType = chars;//���������
	cons[0].bLhsIsAttr = 1;
	cons[0].bRhsIsAttr = 0;
	cons[0].compOp = EQual;
	cons[0].LattrOffset = 0;
	cons[0].LattrLength = 21;
	cons[0].Rvalue = relations[tablenum];
	cons[1].attrType = chars;//���������
	cons[1].bLhsIsAttr = 1;
	cons[1].bRhsIsAttr = 0;
	cons[1].compOp = EQual;
	cons[1].LattrOffset = 21;
	cons[1].LattrLength = 21;
	RM_OpenFile("SYSCOLUMNS", handle);
	for(int i=0;i<nConditions;i++)
	{
		if(conditions[i].bLhsIsAttr==1&&conditions[i].bRhsIsAttr == 1)//����=����
		{
			
			if (!strcmp(conditions[i].lhsAttr.relName,relations[tablenum]))//���Ϊ��ǰ������
 			{ 
 				cons[1].Rvalue = conditions[i].lhsAttr.attrName; //��������
 				connecttable.relName = conditions[i].rhsAttr.relName; 
				connecttable.attrName = conditions[i].rhsAttr.attrName; 
 			} 
			else if (!strcmp(conditions[i].rhsAttr.relName,relations[tablenum]))//�ұ�Ϊ��ǰ������
 			{ 
 				cons[1].Rvalue = conditions[i].rhsAttr.attrName; 
 				connecttable.relName = conditions[i].lhsAttr.relName; 
				connecttable.attrName = conditions[i].lhsAttr.attrName; 
 			} 
			else continue;//�����뵱ǰ���޹�
			int j;
			for(j=0;j<nRelations;j++)//�ҵ������ӱ�λ��
			{
				if(!strcmp(relations[nRelations-j-1],connecttable.relName))
				break;
			}
			if(nRelations-j-1<tablenum)//ֻ�迼�ǻ�δ������ı�
				continue;
			ntable=j;
			op=1;
		}
		else if(conditions[i].bLhsIsAttr==1)//����=ֵ
		{
			if(!strcmp(conditions[i].lhsAttr.relName, relations[tablenum]))
			{
				cons[1].Rvalue = conditions[i].lhsAttr.attrName;
				op=2;
			}
			else	continue;

		}
		else//ֵ=����
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
		case 1://����=����
					{
			RM_FileHandle *temhandle=new RM_FileHandle;
			temhandle->bOpen=false;
			RM_FileScan *temscan=new RM_FileScan;
			temscan->bOpen=false;
			RM_Record *temrec=new RM_Record;
			temrec->bValid=false;
			(*tablecons+ncons)->bLhsIsAttr = 1; //���Ϊ��ǰ��
		    (*tablecons+ncons)->bRhsIsAttr = 0;  		 
			(*tablecons+ncons)->compOp = conditions[i].op;  
 		   	memcpy(&(*tablecons+ncons)->LattrLength, rec->pData + 46, 4); //��¼��ǰ����Ϣ
 		   	memcpy(&(*tablecons+ncons)->LattrOffset, rec->pData + 50, 4); 
			memcpy(&(*tablecons+ncons)->attrType, rec->pData + 42, 4);
			RM_OpenFile("SYSCOLUMNS",temhandle);
			cons[0].Rvalue = connecttable.relName; 
			cons[1].Rvalue = connecttable.attrName; 
			OpenScan(temscan,temhandle,2,cons);
			GetNextRec(temscan,temrec);//�ҵ������ӱ��¼ ��ȡĿ������ƫ����
			int inoffset;
			memcpy(&inoffset,temrec->pData+50,4);
			memcpy(&((*tablecons+ncons)->attrType),temrec->pData+42,4);//����
			(*tablecons+ncons)->Rvalue=tem+offset[ntable]+inoffset;//��ȡ�����ӱ��¼ֵ �ܱ���ʼ��ַ+����ƫ����
			CloseScan(temscan);
			free(temscan);
			free(temhandle);
			free(temrec);
		} 	
			break;
		case 2://����=ֵ
			(*tablecons+ncons)->bLhsIsAttr = 1; 
		    (*tablecons+ncons)->bRhsIsAttr = 0;  		 
			(*tablecons+ncons)->compOp = conditions[i].op;  //������
			memcpy(&(*tablecons+ncons)->LattrLength, rec->pData + 46, 4); 
			memcpy(&(*tablecons+ncons)->LattrOffset, rec->pData + 50, 4); 
 			(*tablecons+ncons)->attrType = conditions[i].rhsValue.type; 
			(*tablecons+ncons)->Rvalue = conditions[i].rhsValue.data; 
			break;
		case 3://ֵ=����
			(*tablecons+ncons)->bLhsIsAttr = 0; 
			(*tablecons+ncons)->bRhsIsAttr = 1; 	
			(*tablecons+ncons)->compOp = conditions[i].op; 
			memcpy(&(*tablecons+ncons)->RattrLength, rec->pData + 46, 4); //����
 		   	memcpy(&(*tablecons+ncons)->RattrOffset, rec->pData + 50, 4); //ƫ����
		   	(*tablecons+ncons)->attrType = conditions[i].lhsValue.type; //����
		   	(*tablecons+ncons)->Lvalue = conditions[i].lhsValue.data; //����
			break;
		default :
			return FAIL;
		}
		CloseScan(scan);
		ncons++;//ɸѡ����������һ
	}
	RM_CloseFile(handle);
	RM_OpenFile(relations[tablenum],handle);
	OpenScan(scan,handle,ncons,*tablecons);
	while(!GetNextRec(scan,rec))//�ݹ���� ����ÿһ����¼
	{
		char *temresult=(char*)malloc(offset[nRelations-tablenum]*sizeof(char));
		if(tem)
		memcpy(temresult,tem,offset[nRelations-tablenum-1]); 
		memcpy(temresult+offset[nRelations-tablenum-1], rec->pData, handle->subhead->recordSize); 
		mutiresult(nSelAttrs,selAttrs,nRelations,relations,nConditions,conditions,res,tablenum-1,temresult,offset);//��������ı�
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
	int offset[10];//���10��
	RM_FileHandle **filehandle=(RM_FileHandle**)malloc(10*(sizeof(RM_FileHandle*)));
	offset[0]=0;
	for(int i=1;i<nRelations+1;i++)
	{
		filehandle[i-1]=new RM_FileHandle;
		filehandle[i-1]->bOpen=false;
		RM_OpenFile(relations[nRelations-i],filehandle[i-1]);
		offset[i]=offset[i-1]+filehandle[i-1]->subhead->recordSize;//��ȡÿ���������ܱ��ڵ���ʼ��ַ
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
	cons[0].attrType = chars;//���������
	cons[0].bLhsIsAttr = 1;
	cons[0].bRhsIsAttr = 0;
	cons[0].compOp = EQual;
	cons[0].LattrOffset = 0;
	cons[0].LattrLength = 21;
	cons[1].attrType = chars;//���������
	cons[1].bLhsIsAttr = 1;
	cons[1].bRhsIsAttr = 0;
	cons[1].compOp = EQual;
	cons[1].LattrOffset = 21;
	cons[1].LattrLength = 21;
	if (!strcmp((*selAttrs)->attrName, "*")){//��ѯ���Ϊ��������
	}else{//��ѯ���Ϊ�ض�����
		resHead->col_num = nSelAttrs;
		resHead->row_num=0;
		if(RM_OpenFile("SYSCOLUMNS", handle)!= SUCCESS) return SQL_SYNTAX;
    	for (int i = 0; i < nSelAttrs; i++){
			cons[0].Rvalue = (selAttrs[nSelAttrs - i - 1])->relName;//����
		    cons[1].Rvalue = (selAttrs[nSelAttrs - i - 1])->attrName;//������
		    OpenScan(scan, handle, 2, cons);
		    if (GetNextRec(scan, rec)!= SUCCESS) return SQL_SYNTAX;
		    char * column = rec->pData;
		    memcpy(&resHead->type[i], column + 42, sizeof(int));//����
		    memcpy(&resHead->fields[i], column + 21, 21);//����
		    memcpy(&resHead->offset[i], column + 50, sizeof(int));//ƫ����
			int j;
			for(j=0;j<nRelations;j++)
			{
				if(!strcmp(relations[nRelations-j-1],(char*)cons[0].Rvalue))
					break;
			}
			resHead->offset[i]+=offset[j];//���ϱ���ƫ��
		    memcpy(&resHead->length[i], column + 46, sizeof(int));//����
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
	RC rc;//���� �� ����
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
			if(GetNextRec(scan,rec)) break;//δ�ҵ�
			if(strcmp(rec->pData,relations[i])==0)//�ҵ�
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
	if(nRelations==1)//�����ѯ
	{
		if(nConditions==0)//����������
		{
			if(rc=singletable_nocon(nSelAttrs,selAttrs,nRelations,relations,res))//ʡ����������
				return rc;
		}
		else//����������
		{
			singletable_cons(nSelAttrs,selAttrs,nRelations,relations,nConditions,conditions,res);
		}
	}
	else//����ѯ
	{
		multitable_nocon(nSelAttrs,selAttrs,nRelations,relations,nConditions,conditions,res);
	}
	return rc;
}


BOOL IsNeedRec(RM_Record *rec,int n,Con *selections){
	bool flag = TRUE;  //����������ֱ���˳�
	for(int i = 0;i<n;i++){
		switch(selections->attrType){
			case chars:
				char *Lattr, *Rattr;
				if (selections->bLhsIsAttr == 1)
					{ //���������������
						Lattr = (char *)malloc(sizeof(char)*selections->LattrLength);
						memcpy(Lattr, rec->pData + selections->LattrOffset, selections->LattrLength);
						Lattr[selections->LattrLength] = '\0';
					}
					else { //�����������ֵ
						Lattr = (char *)selections->Lvalue;
					}

					if (selections->bRhsIsAttr == 1)
					{ //�������ұ�������
						Rattr = (char *)malloc(sizeof(char)*selections->LattrLength);
						memcpy(Lattr, rec->pData + selections->RattrOffset, selections->RattrLength);
					}
					else { //�������ұ���ֵ
						Rattr = (char *)selections->Rvalue;
					}

					flag = Compare(selections->compOp, selections->attrType, Lattr, Rattr);
					break;
			case ints:
					int *IntLattr, *IntRattr;
					if (selections->bLhsIsAttr == 1)
					{ //���������������
						IntLattr = (int *)(rec->pData + selections->LattrOffset);
					}
					else { //�����������ֵ
						IntLattr = (int *)selections->Lvalue;
					}

					if (selections->bRhsIsAttr == 1)
					{ //�������ұ�������
						IntRattr = (int *)(rec->pData + selections->RattrOffset);
					}
					else { //�������ұ���ֵ
						IntRattr = (int *)selections->Rvalue;
					}

					flag = Compare(selections->compOp, selections->attrType, IntLattr, IntRattr);
					break;
				case floats:
					float *FloatLattr, *FloatRattr;
					if (selections->bLhsIsAttr == 1)
					{ //���������������
						FloatLattr = (float *)(rec->pData + selections->LattrOffset);
					}
					else { //�����������ֵ
						FloatLattr = (float *)selections->Lvalue;
					}

					if (selections->bRhsIsAttr == 1)
					{ //�������ұ�������
						FloatRattr = (float *)(rec->pData + selections->RattrOffset);
					}
					else { //�������ұ���ֵ
						FloatRattr = (float *)selections->Rvalue;
					}
					flag = Compare(selections->compOp, selections->attrType, FloatLattr, FloatRattr);
					break;

				default:
					break;
		}
		if(!flag) break;  //һ��������������ֱ������ѭ��
	}
	return flag;
}