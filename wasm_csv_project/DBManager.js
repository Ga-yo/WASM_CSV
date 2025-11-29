class DBManager {
  constructor(dbName = 'wasm_csv_db', storeName = 'files') {
    this.dbName = dbName;
    this.storeName = storeName;
    this.db = null;
  }

  async openDb() {
    if (this.db) {
      return this;
    }
    return new Promise((resolve, reject) => {
      const req = indexedDB.open(this.dbName, 1);
      req.onupgradeneeded = (e) => {
        const db = e.target.result;
        if (!db.objectStoreNames.contains(this.storeName)) {
          db.createObjectStore(this.storeName);
        }
      };
      req.onsuccess = (e) => {
        this.db = e.target.result;
        resolve(this);
      };
      req.onerror = (e) => {
        console.error(`Error opening db: ${e.target.error}`);
        reject(e.target.error);
      };
    });
  }

  async putFile(key, file) {
    await this.openDb();
    return new Promise((resolve, reject) => {
      const tx = this.db.transaction(this.storeName, 'readwrite');
      const store = tx.objectStore(this.storeName);
      store.put(file, key);

      tx.oncomplete = () => {
        try {
          sessionStorage.setItem('uploadedFileName', file.name);
          sessionStorage.setItem('uploadedFileSize', String(file.size));
          console.log('DBManager: Session storage updated with file info.');
        } catch (e) {
          console.warn('DBManager: Failed to set session storage', e);
        }
        resolve();
      };

      tx.onerror = (e) => {
        console.error(`Error in transaction for putFile with key ${key}: ${e.target.error}`);
        reject(e.target.error);
      };
    });
  }

  async getFile(key) {
    await this.openDb();
    return new Promise((resolve, reject) => {
      const tx = this.db.transaction(this.storeName, 'readonly');
      const store = tx.objectStore(this.storeName);
      const req = store.get(key);
      req.onsuccess = () => resolve(req.result);
      req.onerror = (e) => {
        console.error(`Error getting file with key ${key}: ${e.target.error}`);
        reject(e.target.error);
      }
    });
  }

  async deleteFile(key) {
    await this.openDb();
    return new Promise((resolve, reject) => {
      const tx = this.db.transaction(this.storeName, 'readwrite');
      const store = tx.objectStore(this.storeName);
      const req = store.delete(key);
      
      req.onsuccess = () => {
        try {
          sessionStorage.removeItem('uploadedFileName');
          sessionStorage.removeItem('uploadedFileSize');
          console.log('DBManager: Session storage cleared.');
        } catch (e) {
          console.warn('DBManager: Failed to clear session storage', e);
        }
        resolve();
      };

      req.onerror = (e) => {
        console.error(`Error deleting file with key ${key}: ${e.target.error}`);
        reject(e.target.error);
      };
    });
  }

  async clear() {
    await this.openDb();
    return new Promise((resolve, reject) => {
      const tx = this.db.transaction(this.storeName, 'readwrite');
      const store = tx.objectStore(this.storeName);
      const req = store.clear();
      req.onsuccess = () => resolve();
      req.onerror = (e) => {
        console.error(`Error clearing object store: ${e.target.error}`);
        reject(e.target.error);
      }
    });
  }
}

export default DBManager;
