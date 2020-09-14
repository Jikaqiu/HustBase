#include "stdafx.h"
#include "IX_Manager.h"
#pragma warning(disable:26812)

int halforder;
unsigned int first_page;

RC OpenIndexScan(IX_IndexScan* indexScan, IX_IndexHandle* indexHandle, CompOp compOp, char* value) {
	if (indexScan->bOpen == true)
		return IX_SCANOPENNED;
	if (indexHandle->bOpen == false)
		return IX_IHCLOSED;
	indexScan->compOp = compOp;
	indexScan->value = value;
	indexScan->bOpen = true;
	indexScan->pIXIndexHandle = indexHandle;
	indexScan->pnNext = 0; //��ʼ��Ϊ0
	int offset = 0;
	bool is_exit = false;
	PageNum startpage;
	switch (compOp) {
	case EQual:
	case GEqual:
	{
		void* pdata = value;
		PF_FileHandle filehandle = indexHandle->fileHandle;
		IX_FileHeader indexfileheader = indexHandle->fileHeader;
		int order = indexfileheader.order;
		int attrlen = indexfileheader.attrLength;

		PF_PageHandle pagehandle;
		GetThisPage(&filehandle, indexfileheader.rootPage, &pagehandle);
		char* pagedata;
		GetData(&pagehandle, &pagedata);
		char* pagekey = pagedata + sizeof(IX_FileHeader) + sizeof(IX_Node);
		IX_Node* pagenode = (IX_Node*)(pagedata + sizeof(IX_FileHeader));
		char* pagerid = pagekey + attrlen * order;
		int myoffset = 0, flag = 0;
		while (true)
		{ //�����ҵ�ҲҪһֱ��Ҷ�ӽڵ����ֹ
			for (myoffset = 0, flag = 0; myoffset < pagenode->keynum; myoffset++)
			{
				int tmp = keycompare(pagekey + attrlen * myoffset, pdata, indexfileheader.attrType);
				if (tmp == 0) { //���
					flag = 1;
					break;
				}
				else if (tmp == 1){ //��ǰ�ڵ�ϴ�
					break;
				}
			}
			if (pagenode->is_leaf == 1)
			{
				if (flag == 1){
					is_exit = true;
				}
				else{
					is_exit = false;
				}
				GetPageNum(&pagehandle, &startpage);
				offset = myoffset;
				break;
			}
			else if (flag == 0 && pagenode->is_leaf == 0)
			{ //δ��Ҷ��
				if (myoffset > 0){
					myoffset--;
				}
			}

			RID* tmprid = (RID*)(pagerid + sizeof(RID) * myoffset);
			PF_PageHandle tmphandle = pagehandle;
			PageNum tmpnum;
			GetPageNum(&pagehandle, &tmpnum);
			GetThisPage(&filehandle, tmprid->pageNum, &pagehandle);
			GetData(&pagehandle, &pagedata);
			if (tmpnum != tmprid->pageNum)
			{
				UnpinPage(&tmphandle);
				tmphandle=pagehandle;
			}
			pagekey = pagedata + sizeof(IX_FileHeader) + sizeof(IX_Node);
			pagenode = (IX_Node*)(pagedata + sizeof(IX_FileHeader));
			pagerid = pagekey + attrlen * order;
		}
		if (is_exit == true)
			indexScan->pnNext = startpage;
		else
			indexScan->pnNext = 0;
		indexScan->ridIx = myoffset;
		break;
	}
	case LEqual:
	{
		//<=ֻ�ܴ�����߿�ʼɨ��
		indexScan->pnNext = indexHandle->fileHeader.first_leaf;
		indexScan->ridIx = 0;
		break;
	}
	case GreatT:
	{
		void* pdata = value;
		PF_FileHandle filehandle = indexHandle->fileHandle;
		IX_FileHeader fileheader = indexHandle->fileHeader;
		int order = fileheader.order;
		int attrlen = fileheader.attrLength;
		PF_PageHandle pagehandle;
		GetThisPage(&filehandle, fileheader.rootPage, &pagehandle);
		char* pagedata;
		char* pagekeys = NULL;
		char* pagerid = NULL;
		GetData(&pagehandle, &pagedata);
		IX_Node* pagenode = (IX_Node*)(pagedata + sizeof(IX_FileHeader));
		int keynum = pagenode->keynum;
		int moffset = 0;
		int flag = 0;
		while (true)
		{
			pagekeys = pagedata + sizeof(IX_FileHeader) + sizeof(IX_Node);
			pagerid = pagekeys + order * attrlen;
			for (moffset = keynum - 1, flag = 0; moffset >= 0; moffset--)
			{
				int tmp = keycompare(pagekeys + moffset * attrlen, pdata, fileheader.attrType);
				if (tmp <= 0) //�ҵ�
					break;
			}

			if (pagenode->is_leaf == 1)
			{
				moffset++;
				offset = moffset;
				GetPageNum(&pagehandle, &startpage);
				break;
			}

			PF_PageHandle tmphandle = pagehandle;
			PageNum tmpnum;
			GetPageNum(&pagehandle, &tmpnum);
			RID* tmprid = (RID*)(pagerid + moffset * sizeof(RID));
			GetThisPage(&filehandle, tmprid->pageNum, &pagehandle);
			GetData(&pagehandle, &pagedata);
			if (tmpnum != tmprid->pageNum)
			{
				UnpinPage(&tmphandle);
				tmphandle = pagehandle;
			}
			pagenode = (IX_Node*)(pagedata + sizeof(IX_FileHeader));
			keynum = pagenode->keynum;
		}
		indexScan->pnNext = startpage;
		indexScan->ridIx = offset;
		break;
	}
	case NEqual:
	case LessT:
	case NO_OP:
	{
		indexScan->pnNext = indexHandle->fileHeader.first_leaf;
		indexScan->ridIx = 0;
		break;
	}
	default:
		break;
	}
	return SUCCESS;
}


RC IX_GetNextEntry(IX_IndexScan* indexScan, RID* rid) {
	if (indexScan->bOpen == false) {
		//rid = NULL;
		return IX_SCANCLOSED;
	}
	else
	{
		int ridIx = indexScan->ridIx;
		PageNum pagenum = indexScan->pnNext;
		char* value = indexScan->value;
		if (pagenum == 0) {
			return FAIL;
		}
		else
		{
			PF_FileHandle filehandle = indexScan->pIXIndexHandle->fileHandle;
			IX_FileHeader indexfileheader = indexScan->pIXIndexHandle->fileHeader;

			int order = indexfileheader.order;
			int attrlen = indexfileheader.attrLength;
			//��ȡҳ��
			PF_PageHandle pagehandle;
			GetThisPage(&filehandle, pagenum, &pagehandle);
			char* pagedata;
			GetData(&pagehandle, &pagedata);
			IX_Node* pagenode = (IX_Node*)(pagedata + sizeof(IX_FileHeader));
			char* pagekey;
			char* pagerid;
			if (pagenode->keynum == ridIx) {
				//��һҳ������ˣ�ת����һҳ
				UnpinPage(&pagehandle);
				pagenum = pagenode->brother;
				if (pagenode->brother == 0) {
					return IX_EOF;
				}
				GetThisPage(&filehandle, pagenum, &pagehandle);
				GetData(&pagehandle, &pagedata);

				pagenode = (IX_Node*)(pagedata + sizeof(IX_FileHeader));
				pagekey = pagedata + sizeof(IX_FileHeader) + sizeof(IX_Node);
				pagerid = pagekey + order * attrlen;
				ridIx = 0;//��ҳ��ĵ�һ���ڵ�
			}
			else {
				pagekey = pagedata + sizeof(IX_FileHeader) + sizeof(IX_Node);
				pagerid = pagekey + order * attrlen;
			}

			//��ʼcompop
			int tmp = keycompare(pagekey + ridIx * attrlen, indexScan->value, indexfileheader.attrType);
			switch (indexScan->compOp)
			{
			case EQual:
			{
				if (tmp == 0)
				{
					memcpy(rid, pagerid + ridIx * sizeof(RID), sizeof(RID));
					ridIx++;
					indexScan->ridIx = ridIx;
				}
				else
				{
					return IX_EOF;
				}
				break;
			}
			case GEqual:
			case GreatT:
			case NO_OP:
			{ //��ʱ�����нڵ㶼����
				memcpy(rid, pagerid + ridIx * sizeof(RID), sizeof(RID));
				ridIx++;
				indexScan->ridIx = ridIx;
				break;
			}
			case LEqual:
			{
				if (tmp != 1)
				{
					memcpy(rid, pagerid + ridIx * sizeof(RID), sizeof(RID));
					ridIx++;
					indexScan->ridIx = ridIx;
				}
				else
					//rid = NULL;
					return IX_EOF;
				break;
			}
			case LessT:
			{
				if (tmp == -1)
				{
					memcpy(rid, pagerid + ridIx * sizeof(RID), sizeof(RID));
					ridIx++;
					indexScan->ridIx = ridIx;
				}
				else
					//	rid = NULL;
					return IX_EOF;
				break;
			}
			case NEqual:
			{
				if (tmp != 0)
				{
					memcpy(rid, pagerid + ridIx * sizeof(RID), sizeof(RID));
					ridIx++;
					indexScan->ridIx = ridIx;
				}
				else
					//rid = NULL;
					return IX_EOF;
				break;
			}
			default:
				break;
			}
			UnpinPage(&pagehandle);
		}
	}
	return SUCCESS;
}


RC CloseIndexScan(IX_IndexScan* indexScan) {
	if (indexScan->bOpen == true)
		indexScan->bOpen = false;

	return SUCCESS;
}


RC CreateIndex(const char* fileName, AttrType attrType, int attrLength)
{
	RC tmp;
	if ((tmp=CreateFile(fileName)) != SUCCESS){
		return tmp;
	} 
	PF_FileHandle* fileHandle;
	fileHandle = (PF_FileHandle*)malloc(sizeof(PF_FileHandle));
	if (fileHandle == NULL) {
		free(fileHandle);
		return FAIL;
	}
	char* _fileName = const_cast<char*>(fileName);
	openFile(_fileName, fileHandle);    //��ȡһ���ļ�������������ļ��������Ըþ��Ϊ��ʶ

	PF_PageHandle* pageHandle = NULL;
	pageHandle = (PF_PageHandle*)malloc(sizeof(PF_PageHandle));
	if ((tmp=AllocatePage(fileHandle, pageHandle)) != SUCCESS) {
		free(fileHandle);
		free(pageHandle);
		return tmp;
	}
	PageNum pagenum;
	char* pdata;
	GetPageNum(pageHandle, &pagenum);
	GetData(pageHandle, &pdata); 
	IX_FileHeader index_fileheader;
	index_fileheader.attrType = attrType;
	index_fileheader.attrLength = attrLength;
	index_fileheader.rootPage = pagenum;
	index_fileheader.first_leaf = pagenum;
	first_page = pagenum;
	index_fileheader.keyLength = attrLength + sizeof(RID); 
	
	index_fileheader.order = (PF_PAGE_SIZE - sizeof(IX_FileHeader) - sizeof(IX_Node)) / (2 * sizeof(RID) + attrLength);
	halforder = index_fileheader.order / 2;
	memcpy(pdata, &index_fileheader, sizeof(IX_FileHeader));

	IX_Node index_node;
	index_node.is_leaf = 1;
	index_node.keynum = 0;
	index_node.parent = 0;
	index_node.brother = 0;
	memcpy(pdata + sizeof(IX_FileHeader), &index_node, sizeof(IX_Node));

	MarkDirty(pageHandle); 
	UnpinPage(pageHandle);
	CloseFile(fileHandle);
	free(fileHandle);
	free(pageHandle);
	return SUCCESS;
}

RC GetIndexTree(char* fileName, Tree* index)
{
	IX_IndexHandle* indexHandle = new IX_IndexHandle;
	OpenIndex(fileName, indexHandle);

	PageNum rootPage = indexHandle->fileHeader.rootPage;
	index->attrType = indexHandle->fileHeader.attrType;
	index->attrLength = indexHandle->fileHeader.attrLength;
	index->order = indexHandle->fileHeader.order;
	index->root = new Tree_Node;
	handleNodeInfo(indexHandle, rootPage, index->root);
	handleNodeSibling(index->root);

	CloseIndex(indexHandle);
	return SUCCESS;
}


RC OpenIndex(const char* fileName, IX_IndexHandle* indexHandle)
{
	if(indexHandle->bOpen==true)
		return IX_IHOPENNED;
	PF_FileHandle fileHandle;
	RC rtn;
	
	if((rtn=openFile((char*)fileName, &fileHandle))!=SUCCESS){
		return rtn;
	}
	PF_PageHandle* pagehandle;
	pagehandle = (PF_PageHandle*)malloc(sizeof(PF_PageHandle));
	if((rtn=GetThisPage(&fileHandle, 1, pagehandle))!=SUCCESS){
		return rtn; 
	}
	char* pdata;
	GetData(pagehandle, &pdata);  //pageHandle->pFrame->page.pData;
	IX_FileHeader index_header;
	memcpy(&index_header, pdata, sizeof(IX_FileHeader));
	indexHandle->bOpen = true;
	indexHandle->fileHandle = fileHandle; 
	indexHandle->fileHeader = index_header;
	UnpinPage(pagehandle);
	free(pagehandle);
	return SUCCESS;
}

RC CloseIndex(IX_IndexHandle* indexHandle)
{
	if (indexHandle->bOpen == false) {  //����ļ�δ����
		return IX_ISCLOSED;
	}
	indexHandle->bOpen = false;
	return CloseFile(&(indexHandle->fileHandle));
}

RC InsertEntry(IX_IndexHandle* indexHandle, void* pData, RID* rid)
{
	if (indexHandle->bOpen == false || indexHandle->fileHandle.bopen == false){
		return IX_IHCLOSED;
	}

	PF_FileHandle filehandle = indexHandle->fileHandle;
	IX_FileHeader index_fileheader = indexHandle->fileHeader;
	PF_PageHandle pagehandle;
	GetThisPage(&filehandle, index_fileheader.rootPage, &pagehandle);
	PageNum tmppage = index_fileheader.rootPage;
	PF_PageHandle tmphandle = pagehandle;
	char* rootpdata;
	GetData(&pagehandle, &rootpdata);
	IX_Node* rootNodecontrol = (IX_Node*)(rootpdata + sizeof(IX_FileHeader));
	int attrlen = index_fileheader.attrLength;
	while (rootNodecontrol->is_leaf != 1) {
		RID tmprid;
		insertToPage(&pagehandle, index_fileheader.order, index_fileheader.attrType, index_fileheader.attrLength, pData, tmprid, false);
		GetThisPage(&filehandle, tmprid.pageNum, &pagehandle);//��ȡ��ҳ��ľ��
		if (tmprid.pageNum != tmppage) //���µ���ҳ���unpinpageԭpage
		{
			UnpinPage(&tmphandle);
			tmphandle = pagehandle;
		}
		GetData(&pagehandle, &rootpdata);
		rootNodecontrol = (IX_Node*)(rootpdata + sizeof(IX_FileHeader));
	}
	//����Ҷ�ӣ�����ڵ�
	int order = index_fileheader.order;
	insertToPage(&pagehandle, order, index_fileheader.attrType, index_fileheader.attrLength, pData, *rid, true);

	while (rootNodecontrol->keynum == order) {
		//�ڵ����
		PageNum nodepage;
		PageNum newleafpagenum;
		PF_PageHandle* newleafpagehandle = (PF_PageHandle*)malloc(sizeof(PF_PageHandle));
		GetPageNum(&pagehandle, &nodepage);

		AllocatePage(&filehandle, newleafpagehandle);
		GetPageNum(newleafpagehandle, &newleafpagenum);

		if (rootNodecontrol->parent == 0) {
			//���ڵ��� 
			PF_PageHandle* newrootpagehandle = (PF_PageHandle*)malloc(sizeof(PF_PageHandle));
			AllocatePage(&filehandle, newrootpagehandle);
			PageNum newrootpagenum;
			GetPageNum(newrootpagehandle, &newrootpagenum);
			//�����µĸ��ڵ�
			int div1 = rootNodecontrol->keynum / 2;
			int div2 = rootNodecontrol->keynum - div1;
			handleInfo(newrootpagehandle, 0, 0, 0, 0); //�µĸ�
			handleInfo(newleafpagehandle, newrootpagenum, rootNodecontrol->brother, rootNodecontrol->is_leaf, div2); //�µ��ұ߽ڵ�
			handleInfo(&pagehandle, newrootpagenum, newleafpagenum, rootNodecontrol->is_leaf, div1); //�µ���߽ڵ�
			//���£����������ĵ�һ��Ҷ�ӽڵ�ҳ���
			//����Ҷ��Ҷ�ڵ��key��rid
			char* newleafpagedata;
			GetData(newleafpagehandle, &newleafpagedata);
			newleafpagedata = newleafpagedata + sizeof(IX_FileHeader) + sizeof(IX_Node); //ָ���һ��key
			//����key 
			char* oldroot_data;
			GetData(&pagehandle, &oldroot_data);
			char* keys = oldroot_data + sizeof(IX_FileHeader) + sizeof(IX_Node);
			char* rids = keys + order * attrlen;
			memcpy(newleafpagedata, keys + (div1 - 1) * attrlen, div2 * attrlen);
			//����rid
			memcpy(newleafpagedata + order * attrlen, rids + (div1 - 1) * sizeof(RID), div2 * sizeof(RID));
			//���ø��ڵ��rid��key
			char* tmpdata;
			GetData(&pagehandle, &tmpdata); //��ȡ�µ�Ҷ�ӽڵ��һ����Ϣ������ڵ�
			char* insertdata = tmpdata + sizeof(IX_FileHeader) + sizeof(IX_Node);
			RID tmprid;
			tmprid.bValid = false;
			tmprid.pageNum = nodepage;
			char* newrootdata;
			GetData(newrootpagehandle, &newrootdata);
			IX_Node* newrootnodecontrol = (IX_Node*)(newrootdata + sizeof(IX_FileHeader));
			insertToPage(newrootpagehandle, order, index_fileheader.attrType, index_fileheader.attrLength, insertdata, tmprid, true);//�ڸ��ڵ��в������������Ϣ
			tmprid.pageNum = newleafpagenum;
			insertToPage(newrootpagehandle, order, index_fileheader.attrType, index_fileheader.attrLength, newleafpagedata, tmprid, true);//�ڸ��ڵ�����ұ�������Ϣ
			indexHandle->fileHeader.rootPage = newrootpagenum;
			first_page = newrootpagenum;
			MarkDirty(newrootpagehandle);
			MarkDirty(newleafpagehandle);
			UnpinPage(newrootpagehandle);
			UnpinPage(newleafpagehandle);
			free(newrootpagehandle);
		}
		else {
			//���Ǹ��ڵ㣬˵����Ҷ�ӽڵ�
			int div1 = rootNodecontrol->keynum / 2;
			int div2 = rootNodecontrol->keynum - div1;
			PageNum parentpagenum = rootNodecontrol->parent;
			handleInfo(newleafpagehandle, parentpagenum, rootNodecontrol->brother, rootNodecontrol->is_leaf, div2); //�����½ڵ��broΪԴ�ڵ�bro
			handleInfo(&pagehandle, parentpagenum, newleafpagenum, rootNodecontrol->is_leaf, div1); //����Դ�ڵ�broΪ�½ڵ�
			char* newleafpagedata;
			GetData(newleafpagehandle, &newleafpagedata);
			newleafpagedata = newleafpagedata + sizeof(IX_FileHeader) + sizeof(IX_Node); //ָ���һ��key
			char* nowroot_data;
			GetData(&pagehandle, &nowroot_data);
			char* keys = nowroot_data + sizeof(IX_FileHeader) + sizeof(IX_Node);
			char* rids = keys + order * attrlen;
			memcpy(newleafpagedata, keys + (div1 - 1) * attrlen, div2 * attrlen);
			//����rid
			memcpy(newleafpagedata + order * attrlen, rids + (div1 - 1) * sizeof(RID), div2 * sizeof(RID));
			RID tmprid;
			tmprid.bValid = false;
			tmprid.pageNum = newleafpagenum;
			tmphandle = pagehandle; //ָ��ԭҳ��
			GetThisPage(&filehandle, parentpagenum, &pagehandle); //pagehandle����Ϊ���ڵ�
			if (parentpagenum != nodepage)
			{ //���¹�����
				MarkDirty(&tmphandle);
				UnpinPage(&tmphandle);
				tmphandle = pagehandle; //ָ����ҳ��
			}
			insertToPage(&pagehandle, order, index_fileheader.attrType, index_fileheader.attrLength, newleafpagedata, tmprid, true);  //Ϊ��ǰ�ڵ�ĸ��ڵ�������Ϣ
			GetData(&pagehandle, &rootpdata);
			rootNodecontrol = (IX_Node*)(rootpdata + sizeof(IX_FileHeader));  //���ڵ�control��Ϣ
			MarkDirty(newleafpagehandle);
			UnpinPage(newleafpagehandle);
		}
	}
	MarkDirty(&pagehandle);
	UnpinPage(&pagehandle);
	return SUCCESS;
}
RC DeleteEntry(IX_IndexHandle* indexHandle, void* pData, RID* rid)
{
	return SUCCESS;
}
/*
RC DeleteEntry(IX_IndexHandle* indexHandle, void* pData, RID* rid) {
	if (indexHandle->bOpen == false || indexHandle->fileHandle.bopen == false){
		return IX_IHCLOSED;
	}
	PF_FileHandle filehandle = indexHandle->fileHandle;
	IX_FileHeader indexfileheader = indexHandle->fileHeader;

	PF_PageHandle pagehandle;
	GetThisPage(&filehandle, indexfileheader.rootPage, &pagehandle);
	PageNum tmppage = indexfileheader.rootPage;
	PF_PageHandle tmphandle = pagehandle;
	//��ȡ���ڵ�ҳ��
	char* rootpagedata;
	GetData(&pagehandle, &rootpagedata);
	IX_Node* nodecontrol = (IX_Node*)(rootpagedata + sizeof(IX_FileHeader));
	int attrlen = indexfileheader.attrLength;
	int order = indexfileheader.order;
	while (nodecontrol->is_leaf != 1) {
		RID tmprid;
		bool is_find = false;
		getDeleteInfo(rootpagedata, nodecontrol, indexfileheader.attrType, attrlen, order, pData, tmprid, is_find);
		if (is_find == false)
			return IX_EOF;
		else
		{
			GetThisPage(&filehandle, tmprid.pageNum, &pagehandle);
			if (tmprid.pageNum != tmppage)
			{
				UnpinPage(&tmphandle);
				tmphandle = pagehandle;
			}
			GetData(&pagehandle, &rootpagedata);
			nodecontrol = (IX_Node*)(rootpagedata + sizeof(IX_FileHeader));
		}

	}

	//���ڵ�Ҷ�ӽڵ���Ѱ��
	int keynum = nodecontrol->keynum;
	char* leafkey;
	char* leafrid;
	leafkey = rootpagedata + sizeof(IX_FileHeader) + sizeof(IX_Node);
	leafrid = leafkey + order * attrlen;
	int offset = 0;
	int flag = 0; //�Ƿ��ҵ�
	switch (indexfileheader.attrType) {
	case chars:
	{
		char* tmpcomparechar = (char*)pData;
		for (; offset < keynum; offset++)
		{
			char* tmpkeychar = (char*)(leafkey + offset * attrlen);
			int tmp = strcmp(tmpkeychar, tmpcomparechar);
			if (tmp > 0)
				break;
			if (tmp == 0 && comparerid(*((RID*)(leafrid + offset * sizeof(RID))), *rid))
				flag = 1;
			break;
		}
		break;
	}

	case ints:
	{
		int* tmpcompareint = (int*)pData;
		for (; offset < keynum; offset++)
		{
			int* tmpkeyint = (int*)(leafkey + offset * attrlen);
			if ((*tmpkeyint) > (*tmpcompareint))
				break; //flag=0
			if ((*tmpkeyint) == (*tmpcompareint) && comparerid(*((RID*)(leafrid + offset * sizeof(RID))), *rid))
			{
				flag = 1;
				break;
			}
		}
		break;
	}

	case floats:
	{
		float* tmpcomparefloat = (float*)pData;
		for (; offset < keynum; offset++) {
			float* tmpkeyfloat = (float*)(leafkey + offset * attrlen);
			if ((*tmpkeyfloat) > (*tmpcomparefloat))
				break;
			if ((*tmpkeyfloat) == (*tmpcomparefloat) && comparerid(*((RID*)(leafrid + offset * sizeof(RID))), *rid))
				flag = 1;
			break;
		}
		break;
	}
	}

	if (flag == 0) {
		return FAIL;
	}
	else {
		//�ƶ�ָ��ɾ���ؼ���
		memcpy(leafkey + offset * attrlen, leafkey + (offset + 1) * attrlen, (keynum - offset - 1) * attrlen);
		memcpy(leafrid + offset * sizeof(RID), leafrid + (offset + 1) * sizeof(RID), sizeof(RID) * (keynum - offset - 1));
		keynum--;
		nodecontrol->keynum = keynum;
		//��Ҷ������ڵ����,��������ϲ��ڵ�
		while (nodecontrol->parent != 0) {
			int status = 0;
			if (keynum < halforder) {
				PageNum leftpagenum;
				//����leftbro
				AttrType type = indexfileheader.attrType;
				getLeftBro(&pagehandle, &filehandle, order, type, attrlen, leftpagenum);
				if (leftpagenum != 0) {
					//����left
					PF_PageHandle* leftpagehandle = NULL;
					GetThisPage(&filehandle, leftpagenum, leftpagehandle);
					handleLeft(&pagehandle, leftpagehandle, order, type, attrlen, status);
					//���¹�page
					MarkDirty(leftpagehandle);
					UnpinPage(leftpagehandle);
				}
				else {
					//�� right bro��
					PF_PageHandle* rightpagehandle=NULL;
					char* thispagedata;
					GetData(&pagehandle, &thispagedata);
					IX_Node* thispagenode = (IX_Node*)(thispagedata + sizeof(IX_FileHeader));
					GetThisPage(&filehandle, thispagenode->brother, rightpagehandle);
					handleRight(&pagehandle, rightpagehandle, order, type, attrlen, status);

					char* rightpagedata;
					GetData(rightpagehandle, &rightpagedata);
					IX_Node* rightnode = (IX_Node*)(rightpagedata + sizeof(IX_FileHeader));
					PageNum rightpagenum;
					GetPageNum(rightpagehandle, &rightpagenum);
					PF_PageHandle* parentpagehandle=NULL;
					GetThisPage(&filehandle, rightnode->parent, parentpagehandle);
					char* rightkey = rightpagedata + sizeof(IX_FileHeader) + sizeof(IX_Node);
					if (status == 3) {
						//˵�����ұ߽���һ��
						//��Ҫ�޸ĸ��ڵ��������Ϣ
						deleteChild(parentpagehandle, &filehandle, order, type, attrlen, rightpagenum, false, rightkey);
					}
					MarkDirty(rightpagehandle);
					UnpinPage(rightpagehandle);
				}
			}
			PF_PageHandle* parentpagehandle;
			parentpagehandle = (PF_PageHandle*)malloc(sizeof(PF_PageHandle));
			GetThisPage(&filehandle, nodecontrol->parent, parentpagehandle);
			PageNum thispagenum;
			GetPageNum(&pagehandle, &thispagenum);

			if (status == 1 || offset == 0) {
				//����bro�����ָ��Ҷ�ӵ�����߽ڵ�,��Ҫ�޸�������Ϣ
				deleteChild(parentpagehandle, &filehandle, order, indexfileheader.attrType, attrlen, thispagenum, false, leafkey);
				break;
			}
			else if (status == 2) {
				//����ڵ�����˺ϲ�
				//���¸��ڵ�������Ϣ��ɾ��һ������
				deleteChild(parentpagehandle, &filehandle, order, indexfileheader.attrType, attrlen, thispagenum, true, NULL);
				pagehandle = (*parentpagehandle);//�����ӽڵ�Ϊ��ǰ���ڵ�
				GetData(&pagehandle, &rootpagedata);//���½ڵ�
				nodecontrol = (IX_Node*)(rootpagedata + sizeof(IX_FileHeader));
				leafkey = rootpagedata + sizeof(IX_FileHeader) + sizeof(IX_Node);
				keynum = nodecontrol->keynum;
			}
			else if (status == 4) {
				//���ұ߽ڵ�ϲ�
				if (offset == 0) {
					//����ɾ�����ǵ�ǰҳ���һ���ڵ�
					//��Ҫ���¸��ڵ�������Ϣ
					deleteChild(parentpagehandle, &filehandle, order, indexfileheader.attrType, attrlen, thispagenum, false, leafkey);
					offset = (offset == 0 ? 1 : offset); //��ֹ�ظ�����
				}
				//�ҽڵ㱻�ϲ���Ҫ�޸ĸ��׽ڵ�����
				GetThisPage(&filehandle, nodecontrol->brother, &pagehandle);
				GetData(&pagehandle, &rootpagedata);
				GetPageNum(&pagehandle, &thispagenum);

				nodecontrol = (IX_Node*)(rootpagedata + sizeof(IX_FileHeader));
				//��ȡ���ڵ�
				PF_PageHandle* tmppage = parentpagehandle;
				GetThisPage(&filehandle, nodecontrol->parent, parentpagehandle);
				deleteChild(parentpagehandle, &filehandle, order, indexfileheader.attrType, attrlen, thispagenum, true, NULL);
				pagehandle = (*parentpagehandle);//�ӽڵ�ָ�򸸽ڵ�
				GetData(&pagehandle, &rootpagedata);
				nodecontrol = (IX_Node*)(rootpagedata + sizeof(IX_FileHeader));
				leafkey = rootpagedata + sizeof(IX_FileHeader) + sizeof(IX_Node);
				keynum = nodecontrol->keynum;
				MarkDirty(tmppage);
				UnpinPage(tmppage);

			}
			else {
				free(parentpagehandle);
				break;
			}
			free(parentpagehandle);
		}

	}
	MarkDirty(&pagehandle);
	UnpinPage(&pagehandle);
	return SUCCESS;
}
*/
void deleteChild(PF_PageHandle* parentpagehandle, PF_FileHandle* filehandle, int order, AttrType type, int attrlen, PageNum nodepagenum, bool is_delete, void* pdata)
{
	char* parentdata;
	char* parentkey;
	char* parentrid;
	while (true) {
		GetData(parentpagehandle, &parentdata);
		IX_Node* parentnodecontrol = (IX_Node*)(parentdata + sizeof(IX_FileHeader));
		parentkey = parentdata + sizeof(IX_FileHeader) + sizeof(IX_Node);
		parentrid = parentkey + attrlen * order;

		int keynum = parentnodecontrol->keynum;
		for (int i = 0; i < keynum; i++) {
			RID* tmprid = (RID*)(parentrid + i * sizeof(RID));

			if (tmprid->pageNum == nodepagenum) {
				if (is_delete) {
					memcpy(parentkey + i * attrlen, parentkey + (i + 1) * attrlen, (keynum - i - 1) * attrlen);
					keynum--;
					parentnodecontrol->keynum = keynum;
					return;
				}
				else {
					memcpy(parentkey + i * attrlen, pdata, attrlen);
					if (i == 0 && parentnodecontrol->parent != 0) {
						GetPageNum(parentpagehandle, &nodepagenum); //��ȡ���׽ڵ�ҳ��ź;��
						PF_PageHandle* tmphandle = parentpagehandle;
						GetThisPage(filehandle, parentnodecontrol->parent, parentpagehandle);
						if (nodepagenum != parentnodecontrol->parent)
						{
							MarkDirty(tmphandle);
							UnpinPage(tmphandle);
						}
					}
					else
						return;
				}
			}
		}
	}
}
//markhere
void handleRight(PF_PageHandle* thispagehandle, PF_PageHandle* rightpagehandle, int order, AttrType type, int attrlen, int& status)
{
	char* thisdata;
	GetData(thispagehandle, &thisdata);
	IX_Node* thisnode = (IX_Node*)(thisdata + sizeof(IX_FileHeader));
	char* thiskey = thisdata + sizeof(IX_FileHeader) + sizeof(IX_Node);
	RID* thisrid = (RID*)(thiskey + attrlen * order);

	char* rightdata;
	GetData(rightpagehandle, &rightdata);
	IX_Node* rightnode = (IX_Node*)(rightdata + sizeof(IX_FileHeader));
	char* rightkey = rightdata + sizeof(IX_FileHeader) + sizeof(IX_Node);
	RID* rightrid = (RID*)(rightkey + attrlen * order);

	if (rightnode->keynum > halforder) {
		memcpy(thiskey + attrlen * thisnode->keynum, rightkey, attrlen);
		memcpy(thisrid + thisnode->keynum, rightrid, 1);
		memcpy(rightkey, rightkey + attrlen, (rightnode->keynum - 1) * attrlen);
		memcpy(rightrid, rightrid + 1, (rightnode->keynum - 1));

		rightnode->keynum--;
		thisnode->keynum++;
		status = 3;
	}
	else {
		//�ұ�����ϲ�
		memcpy(thiskey + attrlen * thisnode->keynum, rightkey, attrlen * rightnode->keynum);
		memcpy(thisrid + thisnode->keynum, rightrid, rightnode->keynum);
		thisnode->keynum += rightnode->keynum;
		rightnode->keynum = 0;
		thisnode->brother = rightnode->brother;
		status = 4;
	}
	return;
}

void handleLeft(PF_PageHandle* thispagehandle, PF_PageHandle* leftpagehandle, int order, AttrType type, int attrlen, int& status)
{
	char* thisdata;
	GetData(thispagehandle, &thisdata);
	char* thiskey = thisdata + sizeof(IX_FileHeader) + sizeof(IX_Node);
	char* thisrid = thiskey + attrlen * order;
	IX_Node* thisnode = (IX_Node*)(thisdata + sizeof(IX_FileHeader));
	char* leftdata;
	GetData(leftpagehandle, &leftdata);
	char* leftkey = leftdata + sizeof(IX_FileHeader) + sizeof(IX_Node);
	char* leftrid = leftkey + attrlen * order;
	IX_Node* leftnode = (IX_Node*)(leftdata + sizeof(IX_FileHeader));
	if (leftnode->keynum > halforder) {
		//��leftbro��һ��key rid
		memcpy(thiskey + attrlen * 1, thiskey, thisnode->keynum * attrlen);
		memcpy(thisrid + sizeof(RID) * 1, thisrid, thisnode->keynum * sizeof(attrlen));
		memcpy(thiskey, leftkey + (leftnode->keynum - 1) * attrlen, attrlen);
		memcpy(thisrid, leftrid + (thisnode->keynum - 1) * sizeof(RID), sizeof(RID));
		thisnode->keynum++;
		leftnode->keynum--;
		status = 1;
	}
	else {
		//�ϲ������ڵ� ����������
		memcpy(leftkey + attrlen * leftnode->keynum, thiskey, thisnode->keynum * attrlen);
		memcpy(leftrid + sizeof(RID) * leftnode->keynum, thisrid, sizeof(RID) * thisnode->keynum);
		leftnode->keynum += thisnode->keynum;
		thisnode->keynum = 0;
		leftnode->brother = thisnode->brother;
		status = 2;
	}
	return;
}

void getLeftBro(PF_PageHandle* pagehandle, PF_FileHandle* filehandle, int order, AttrType type, int attrlen, PageNum& pagenum)
{
	PageNum nowpage;
	GetPageNum(pagehandle, &nowpage);
	char* nowdata;
	GetData(pagehandle, &nowdata);
	//ͨ��nowpageȷ��parentpage
	IX_Node* nowpagenode = (IX_Node*)(nowdata + sizeof(IX_FileHeader));
	PageNum parentpagenum = nowpagenode->parent;
	PF_PageHandle* parentpagehandle=NULL;
	GetThisPage(filehandle, parentpagenum, parentpagehandle);
	//ͨ�������ȡdata
	char* parentdata;
	GetData(parentpagehandle, &parentdata);
	char* parentkey;
	char* parentrid;
	parentkey = parentdata + sizeof(IX_FileHeader) + sizeof(IX_Node);
	parentrid = parentkey + attrlen * order;
	//����leftbroҳ���
	IX_Node* parentpagenode = (IX_Node*)(nowdata + sizeof(IX_FileHeader));
	int keynum = parentpagenode->keynum;
	for (int i = 0; i < keynum; i++) {
		RID* tmprid = (RID*)(parentrid + i * sizeof(RID));
		if (i != 0 && tmprid->pageNum == nowpage) {
			i--;
			tmprid = (RID*)(parentrid + i * sizeof(RID));
			pagenum = tmprid->pageNum; //�ҵ�
			UnpinPage(parentpagehandle);
			return;
		}
	}
	//Ϊ�˰�ȫ�����getpage���unpin
	UnpinPage(parentpagehandle);
	pagenum = 0; //������
	return;
}


bool comparerid(const RID& rid1, const RID& rid2)
{
	if (rid1.pageNum == rid2.pageNum && rid1.slotNum == rid2.slotNum && rid1.bValid == rid2.bValid)
		return true;
	return false;
}

void insertToPage(PF_PageHandle* pagehandle, int order, AttrType type, int attrlen, void* pdata, RID& rid, bool istrue)
{
	char* parentData;
	char* parentkeys;
	char* parentRids;
	GetData(pagehandle, &parentData);
	IX_Node* indexnode = (IX_Node*)(parentData + sizeof(IX_FileHeader));
	int keynum = indexnode->keynum;//��ǰ�ؼ��ָ���
	parentkeys = parentData + sizeof(IX_FileHeader) + sizeof(IX_Node);
	parentRids = parentkeys + order * attrlen;  //ע��˴�Ϊorder����keynum
	int offset = 0;
	//��ȡ����λ��
	switch (type)
	{
	case AttrType::chars:
	{
		char* compchar = (char*)pdata;
		for (; offset < keynum; offset++) {
			char* tmpchar = (char*)(parentkeys + offset * attrlen);
			if (strcmp(tmpchar, compchar) > 0)
				break;
		}
		break;
	}
	case AttrType::ints:
	{
		int compint = *(int*)pdata;
		for (; offset < keynum; offset++) {
			int tmpint = *((int*)(parentkeys + offset * attrlen));
			if (tmpint > compint)
				break;
		}
		break;
	}

	case AttrType::floats:
	{
		float compfloat = *(float*)pdata;
		for (; offset < keynum; offset++) {
			float tmpfloat = *((float*)(parentkeys + offset * attrlen));
			if (tmpfloat > compfloat)
				break;
		}
		break;
	}

	}

	if (istrue) {
		//ָ��key�ƶ�
		memcpy(parentkeys + (offset + 1) * attrlen, parentkeys + offset * attrlen, (keynum - offset) * attrlen);
		memcpy(parentkeys + offset * attrlen, pdata, attrlen);
		//rid�ƶ�
		memcpy(parentRids + (offset + 1) * sizeof(RID), parentRids + offset * sizeof(RID), (keynum - offset) * sizeof(RID));
		memcpy(parentRids + offset * sizeof(RID), &rid, sizeof(RID));
		keynum++;
		indexnode->keynum = keynum;
	}
	else {
		offset--;

		if (offset < 0) { //���û�д洢��Ϣ���ᵼ��offset<0
			offset = 0;
			//�浽��һ���ڵ���,���µ�ǰ��Ҷ�ӽڵ�Ĺؼ���
			memcpy(parentkeys, pdata, attrlen);
		}
		//������һ���ڵ��ѯ
		memcpy(&rid, parentRids + offset * sizeof(RID), sizeof(RID));
	}
	return;
}

void handleInfo(PF_PageHandle* newleafpagehandle, PageNum parent, PageNum brother, int is_leaf, int keynum)
{
	IX_Node newnode;
	newnode.brother = brother;
	newnode.parent = parent;
	newnode.is_leaf = is_leaf;
	newnode.keynum = keynum;
	char* pdata;
	GetData(newleafpagehandle, &pdata);
	memcpy(pdata + sizeof(IX_FileHeader), &newnode, sizeof(IX_Node));
	return;

}

void getDeleteInfo(char* rootdata, IX_Node* nodecontrol, AttrType type, int attrlen, int order, void* pdata, RID& rid, bool& is_find)
{
	char* parentkey;
	char* parentrid;
	parentkey = rootdata + sizeof(IX_FileHeader) + sizeof(IX_Node);
	parentrid = rootdata + order * attrlen;
	int keynum = nodecontrol->keynum;//�ڵ����
	int offset = 0;
	int flag = 0;
	switch (type) {
	case AttrType::chars:
	{
		char* tmpcomparechar = (char*)pdata;
		for (; offset < keynum; offset++) {
			char* tmpkeychar = parentkey + offset * attrlen;
			int tmp = strcmp(tmpkeychar, tmpcomparechar);
			if (tmp >= 0) {
				if (tmp == 0)
					flag = 1;
				break;
			}
		}
		break;
	}

	case AttrType::ints:
	{
		int* tmpcompareint = (int*)pdata;
		for (; offset < keynum; offset++) {
			int* tmpkeyint = (int*)(parentkey + offset * attrlen);
			if ((*tmpkeyint) >= (*tmpcompareint)) {
				if ((*tmpkeyint) == (*tmpcompareint))
					flag = 1;
				break;
			}
		}
		break;
	}
	case AttrType::floats:
	{
		float* tmpcomparefloat = (float*)pdata;
		for (; offset < keynum; offset++) {
			float* tmpkeyfloat = (float*)(parentkey + offset * attrlen);
			if ((*tmpkeyfloat) >= (*tmpcomparefloat)) {
				if ((*tmpkeyfloat) == (*tmpcomparefloat))
					flag = 1;
				break;
			}
		}
		break;
	}

	}
	if (flag == 1) {
		is_find = true;
		memcpy(&rid, parentrid + offset * sizeof(RID), sizeof(RID));
	}
	else {
		offset--;
		if (offset < 0) {
			is_find = false;
		}
		else {
			memcpy(&rid, parentrid + offset * sizeof(RID), sizeof(RID));
			is_find = true;
		}
	}
	return;
}


void handleNodeInfo(IX_IndexHandle* indexHandle, PageNum pageNum, Tree_Node* aNode) {
	int keyLength = indexHandle->fileHeader.keyLength;
	int attrLength = indexHandle->fileHeader.attrLength;
	auto currentNodePage = new PF_PageHandle;
	
	GetThisPage(&indexHandle->fileHandle, pageNum, currentNodePage);
	char* src_data;
	GetData(currentNodePage, &src_data);
	auto currentNode = (IX_Node*)(src_data + sizeof(IX_FileHeader));
	char* keys = src_data + sizeof(IX_FileHeader) + sizeof(IX_Node);
	char* rids = keys + indexHandle->fileHeader.order * indexHandle->fileHeader.attrLength;

	aNode->keyNum = currentNode->keynum;
	aNode->keys = new char* [aNode->keyNum];
	for (int i = 0; i < aNode->keyNum; i++) {
		aNode->keys[i] = new char[keyLength];
		memcpy(aNode->keys[i], keys + i * keyLength, sizeof(char) * keyLength);
		// cpy all keys
	}
	aNode->parent = nullptr;
	aNode->sibling = nullptr;

	if (currentNode->is_leaf) {
		aNode->firstChild = nullptr;
		return;
	}
	else {
		aNode->firstChild = new Tree_Node[aNode->keyNum + 1];
	}

	for (int i = 0; i < aNode->keyNum + 1; i++) {
		char* curChild = rids + i * sizeof(PageNum);
		PageNum curChildPageNum = 0;
		memcpy(&curChildPageNum, curChild, sizeof(PageNum));
		//        if (*curChild == (char) 16) {
		//            printf("??");
		//        }
		handleNodeInfo(indexHandle, curChildPageNum, &aNode->firstChild[i]);
		aNode->firstChild[i].parent = aNode;
	} // create all children
}

void handleNodeSibling(Tree_Node* aNode) {
	if (aNode->firstChild == nullptr) {
		return;
	}

	for (int i = 0; i < aNode->keyNum + 1; i++) {
		if (i != aNode->keyNum) {
			aNode->firstChild[i].sibling = &aNode->firstChild[i + 1];
		}
		else {
			if (aNode->sibling != nullptr) {
				aNode->firstChild[i].sibling = &aNode->sibling->firstChild[0];
			}
			else {
				aNode->firstChild[i].sibling = nullptr;
			}
		}
	} // create all sibling

	for (int i = 0; i < aNode->keyNum + 1; i++) {
		handleNodeSibling(&aNode->firstChild[i]);
	} // create for all children
}

int keycompare(void* data1, void* data2, AttrType type)
{
	int ret;
	switch (type) {
	case AttrType::chars:
	{
		int tmp = strcmp((char*)data1, (char*)data2);
		if (tmp > 0)
			ret = 1;
		else if (tmp == 0)
			ret = 0;
		else ret = -1;
		break;
	}
	case AttrType::ints:
	{
		if (*((int*)data1) > * ((int*)data2))
			ret = 1;
		else if (*((int*)data1) == *((int*)data2))
			ret = 0;
		else ret = -1;
		break;
	}
	case AttrType::floats:
	{
		if (*((float*)data1) > * ((float*)data2))
			ret = 1;
		else if (*((float*)data1) == *((float*)data2))
			ret = 0;
		else ret = -1;
		break;
	}
	}
	return ret;
}
